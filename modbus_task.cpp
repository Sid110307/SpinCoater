#include "modbus_task.h"

#include <esp_task_wdt.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "hardware.h"
#include "modbus.h"
#include "rtos.h"

// Each calling task owns one Waiter (kept in its thread-local storage). A ticket makes a late reply to a
// timed-out call harmless, and task notifications stay free for the emergency task's interrupt wake-up.
struct Waiter {
  SemaphoreHandle_t sem;
  volatile uint32_t doneTicket;
  volatile bool ok;
  uint32_t ticket;
};

struct MbRequest {
  MbOp op;
  uint16_t arg;
  Waiter *waiter;
  uint32_t ticket;
};

static QueueHandle_t qNormal = nullptr;
static QueueHandle_t qUrgent = nullptr;

static SemaphoreHandle_t telemetryMutex = nullptr;
static Telemetry telemetryCache;
static OvercurrentAlarm overcurrentAlarmCache;

static bool refreshTelemetry() {
  uint16_t voltage = 0, current = 0, power = 0, speed = 0, fault = 0;
  bool okVoltage = modbusReadVoltage(voltage);
  bool okCurrent = modbusReadCurrent(current);
  bool okPower = modbusReadPower(power);
  bool okSpeed = modbusReadSpeed(speed);
  bool okFault = modbusReadFault(fault);
  bool ok = okVoltage && okCurrent && okPower && okSpeed && okFault;

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  if (okVoltage) telemetryCache.voltage = voltage;
  if (okCurrent) telemetryCache.current = current;
  if (okPower) telemetryCache.power = power;
  if (okSpeed) telemetryCache.speed = speed;
  if (okFault) telemetryCache.fault = fault;
  telemetryCache.valid = ok;
  telemetryCache.anyOk = okVoltage || okCurrent || okSpeed || okFault;
  telemetryCache.updatedMs = millis();
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);

  return ok;
}

static bool refreshOvercurrentAlarm() {
  uint16_t amps = 0;
  bool ok = modbusReadOvercurrentAlarm(amps);

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  if (ok) overcurrentAlarmCache.amps = amps;
  overcurrentAlarmCache.valid = ok;
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);

  return ok;
}

static bool execute(const MbRequest& r) {
  switch (r.op) {
  case MbOp::CommSource: return modbusUseCommunicationControl();
  case MbOp::ControlMode: return modbusSetControlMode(r.arg != 0);
  case MbOp::ClosedLoopRpm: return modbusSetClosedLoopRpm(r.arg);
  case MbOp::OpenLoopPercent: return modbusSetOpenLoopSpeedPercent(r.arg);
  case MbOp::AccelMs: return modbusSetAccelTimeMs(r.arg);
  case MbOp::Run: return modbusSetRun(r.arg != 0);
  case MbOp::Direction: return modbusSetDirection(r.arg != 0);
  case MbOp::Brake: return modbusSetBrake(r.arg != 0);
  case MbOp::ClearFault: return modbusClearFault();
  case MbOp::PolePairs: return modbusSetPolePairs(static_cast<uint8_t>(r.arg));
  case MbOp::CurrentLimit: return modbusSetCurrentLimit(static_cast<uint8_t>(r.arg));
  case MbOp::PwmMax: return modbusSetPwmMax(static_cast<uint8_t>(r.arg));
  case MbOp::RefreshTelemetry: return refreshTelemetry();
  case MbOp::RefreshOvercurrentAlarm: return refreshOvercurrentAlarm();
  }
  return false;
}

static void reply(const MbRequest &r, bool ok) {
  if (!r.waiter) return;
  r.waiter->ok = ok;
  r.waiter->doneTicket = r.ticket;
  xSemaphoreGive(r.waiter->sem);
}

static void modbusTaskFn(void*) {
  esp_task_wdt_add(nullptr);
  MbRequest r;
  for (;;) {
    esp_task_wdt_reset();
    // emergency is first priority
    if (xQueueReceive(qUrgent, &r, 0) == pdTRUE) {
      reply(r, execute(r));
      continue;
    }

    if (xQueueReceive(qNormal, &r, pdMS_TO_TICKS(5)) == pdTRUE) {
      // ignore everything else while emergency is latched
      if (isEmergencyLatched()) {
        reply(r, false);
        continue;
      }
      reply(r, execute(r));
    }
  }
}

static void telemetryTaskFn(void*) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    if (!isEmergencyLatched()) {
      mbCall(MbOp::RefreshTelemetry, 0, pdMS_TO_TICKS(300));
      Telemetry telemetry = mbTelemetry();
      if (telemetry.valid && telemetry.fault != 0) setEmergencyLatched(true);
    } else {
      mbCallUrgent(MbOp::RefreshTelemetry, 0, pdMS_TO_TICKS(300));  // keep the speed reading alive while braking
    }
  }
}

static constexpr int MAX_WAITERS = 10;  // one per task that ever calls mbCall (comms, ramp, safety, ...)
static TaskHandle_t waiterOwner[MAX_WAITERS];
static Waiter waiterSlot[MAX_WAITERS];
static portMUX_TYPE waiterLock = portMUX_INITIALIZER_UNLOCKED;

static Waiter *myWaiter() {
  TaskHandle_t me = xTaskGetCurrentTaskHandle();
  Waiter *found = nullptr;
  portENTER_CRITICAL(&waiterLock);
  for (int i = 0; i < MAX_WAITERS && !found; ++i)
    if (waiterOwner[i] == me) found = &waiterSlot[i];
  for (int i = 0; i < MAX_WAITERS && !found; ++i)
    if (!waiterOwner[i]) {
      waiterOwner[i] = me;
      found = &waiterSlot[i];
    }
  portEXIT_CRITICAL(&waiterLock);
  if (found && !found->sem) found->sem = xSemaphoreCreateBinary();
  return found;
}

static bool submit(QueueHandle_t q, MbOp op, uint16_t arg, TickType_t timeout) {
  if (!q) return false;

  Waiter *w = myWaiter();
  if (!w || !w->sem) return false;
  uint32_t ticket = ++w->ticket;
  while (xSemaphoreTake(w->sem, 0) == pdTRUE) {}  // drop a stale reply from an earlier timed-out call

  MbRequest r{ op, arg, w, ticket };
  if (xQueueSend(q, &r, timeout) != pdTRUE) return false;

  TickType_t start = xTaskGetTickCount();
  for (;;) {
    TickType_t used = xTaskGetTickCount() - start;
    if (used >= timeout || xSemaphoreTake(w->sem, timeout - used) != pdTRUE) return false;
    if (w->doneTicket == ticket) return w->ok;  // otherwise it was a stale reply: keep waiting
  }
}

bool mbCall(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qNormal, op, arg, timeout);
}

bool mbCallUrgent(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qUrgent, op, arg, timeout);
}

Telemetry mbTelemetry() {
  Telemetry copy;

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  copy = telemetryCache;
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);
  return copy;
}

OvercurrentAlarm mbOvercurrentAlarm() {
  OvercurrentAlarm copy;

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  copy = overcurrentAlarmCache;
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);
  return copy;
}

void modbusTaskBegin() {
  qNormal = xQueueCreate(16, sizeof(MbRequest));
  qUrgent = xQueueCreate(8, sizeof(MbRequest));
  telemetryMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(modbusTaskFn, "modbus", 4096, nullptr, PRIO_MODBUS, nullptr, 1);
  xTaskCreatePinnedToCore(telemetryTaskFn, "telemetry", 4096, nullptr, PRIO_TELEMETRY, nullptr, 1);
}

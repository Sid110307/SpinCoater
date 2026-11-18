#include "modbus_task.h"

#include <esp_task_wdt.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "config.h"
#include "hardware.h"
#include "modbus.h"
#include "motor.h"
#include "ota.h"
#include "ramp.h"
#include "rtos.h"
#include <string.h>

struct Waiter {
  SemaphoreHandle_t sem;
  volatile uint32_t doneTicket;
  volatile bool ok;
  uint32_t ticket;
  DriverPacketResult packetResult;
};

static portMUX_TYPE waiterLock = portMUX_INITIALIZER_UNLOCKED;

struct MbRequest {
  MbOp op;
  uint16_t arg;
  Waiter *waiter;
  uint32_t ticket;
  uint8_t packet[DRIVER_PACKET_REQUEST_SIZE]{};
  TickType_t packetQueuedAt = 0;
  TickType_t packetBudget = 0;
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

static bool manualWriteAllowed(const uint8_t *packet) {
  uint16_t reg = (packet[2] << 8) | packet[3];
  uint16_t value = (packet[4] << 8) | packet[5];
  switch (reg) {
    case 0x4001: return value >= POLE_PAIRS_MIN && value <= POLE_PAIRS_MAX;
    case 0x4003: return value >= CURRENT_LIMIT_MIN_A && value <= CURRENT_LIMIT_MAX_A;
    case 0x4004: return value >= PWM_MAX_MIN && value <= PWM_MAX_MAX;
    case 0x4005: return value <= 1;
    case 0x4006: return value >= CLOSED_LOOP_RPM_MIN && value <= CLOSED_LOOP_RPM_MAX;
    case 0x4007: return value >= OPEN_LOOP_RAW_MIN && value <= OPEN_LOOP_RAW_MAX;
    case 0x4011: return value >= ACCEL_MS_MIN && value <= ACCEL_MS_MAX;
    case 0x4034: return value == 0;
    case 0x4035: return value <= 1;
    case 0x4036: return value == 1;
    case 0x4037: return value == 1;
    case 0x4043: return value == 1;
    default: return false;
  }
}

static bool executeDriverPacket(const MbRequest &r, DriverPacketResult &result) {
  result = {};
  TickType_t elapsed = xTaskGetTickCount() - r.packetQueuedAt;
  if (elapsed >= r.packetBudget || r.packetBudget - elapsed <= pdMS_TO_TICKS(100))
    return true;
  if (isMotorRunning() || motorBrakeStopPending() || rampGetState().active || otaInProgress()) {
    result.status = DriverPacketStatus::Busy;
    return true;
  }
  if (r.packet[1] == 0x06) {
    Telemetry t = mbTelemetry();
    PulseSpeedSnapshot pulse = getPulseSpeedSnapshot();
    if (isEmergencyLatched() || physicalSafetyInputsActive() || !t.valid ||
        millis() - t.updatedMs >= COMMS_LOSS_LIMIT_MS || t.fault != 0 ||
        t.speed >= SPINNING_RPM || (pulse.valid && pulse.rpm >= SPINNING_RPM)) {
      result.status = DriverPacketStatus::SafetyLocked;
      return true;
    }
    if (!manualWriteAllowed(r.packet)) {
      result.status = DriverPacketStatus::UnsupportedWrite;
      return true;
    }
  }
  modbusExchangePacket(r.packet, result);
  return true;
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
  case MbOp::DriverPacket: return false;
  }
  return false;
}

static void reply(const MbRequest &r, bool ok, const DriverPacketResult *packetResult = nullptr) {
  if (!r.waiter) return;
  bool current;
  portENTER_CRITICAL(&waiterLock);
  current = r.waiter->ticket == r.ticket;
  if (current) {
    if (packetResult) r.waiter->packetResult = *packetResult;
    r.waiter->ok = ok;
    r.waiter->doneTicket = r.ticket;
  }
  portEXIT_CRITICAL(&waiterLock);
  if (current) xSemaphoreGive(r.waiter->sem);
}

static void executeAndReply(const MbRequest &r) {
  if (r.op == MbOp::DriverPacket) {
    DriverPacketResult result;
    bool ok = executeDriverPacket(r, result);
    reply(r, ok, &result);
  } else {
    reply(r, execute(r));
  }
}

static void modbusTaskFn(void*) {
  esp_task_wdt_add(nullptr);
  MbRequest r;
  for (;;) {
    esp_task_wdt_reset();
    if (xQueueReceive(qUrgent, &r, 0) == pdTRUE) {
      executeAndReply(r);
      continue;
    }

    if (xQueueReceive(qNormal, &r, pdMS_TO_TICKS(5)) == pdTRUE) {
      if (isEmergencyLatched()) {
        reply(r, false);
        continue;
      }
      executeAndReply(r);
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
      mbCallUrgent(MbOp::RefreshTelemetry, 0, pdMS_TO_TICKS(300));
    }
  }
}

static constexpr int MAX_WAITERS = 10;
static TaskHandle_t waiterOwner[MAX_WAITERS];
static Waiter waiterSlot[MAX_WAITERS];

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

static bool submit(QueueHandle_t q, MbOp op, uint16_t arg, TickType_t timeout,
                   const uint8_t *packet = nullptr, DriverPacketResult *packetResult = nullptr) {
  if (!q) return false;

  Waiter *w = myWaiter();
  if (!w || !w->sem) return false;
  portENTER_CRITICAL(&waiterLock);
  uint32_t ticket = ++w->ticket;
  portEXIT_CRITICAL(&waiterLock);
  while (xSemaphoreTake(w->sem, 0) == pdTRUE) {}

  MbRequest r{ op, arg, w, ticket };
  if (packet) {
    memcpy(r.packet, packet, sizeof(r.packet));
    r.packetQueuedAt = xTaskGetTickCount();
    r.packetBudget = timeout;
  }
  if (xQueueSend(q, &r, timeout) != pdTRUE) return false;

  TickType_t start = xTaskGetTickCount();
  for (;;) {
    TickType_t used = xTaskGetTickCount() - start;
    if (used >= timeout || xSemaphoreTake(w->sem, timeout - used) != pdTRUE) return false;
    if (w->doneTicket == ticket) {
      if (w->ok && packetResult) *packetResult = w->packetResult;
      return w->ok;
    }
  }
}

bool mbCall(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qNormal, op, arg, timeout);
}

bool mbCallUrgent(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qUrgent, op, arg, timeout);
}

bool mbCallDriverPacket(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE],
                       DriverPacketResult &result, TickType_t timeout) {
  result = {};
  if (!driverPacketRequestValid(request)) return false;
  QueueHandle_t queue = request[1] == 0x03 ? qUrgent : qNormal;
  bool ok = submit(queue, MbOp::DriverPacket, 0, timeout, request, &result);
  if (!ok && isEmergencyLatched()) result.status = DriverPacketStatus::SafetyLocked;
  return ok;
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

#include "modbus_task.h"

#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "modbus.h"
#include "rtos.h"

struct MbRequest {
  MbOp op;
  uint16_t arg;
  TaskHandle_t notify;
};

static QueueHandle_t qNormal = nullptr;
static QueueHandle_t qUrgent = nullptr;

static SemaphoreHandle_t telemetryMutex = nullptr;
static Telemetry telemetryCache;

static volatile bool emergencyLatched = false;

static bool refreshTelemetry() {
  uint16_t a[2] = { 0, 0 };
  uint16_t b[3] = { 0, 0, 0 };
  bool okA = modbusReadBlock(0x0026, 2, a);
  bool okB = modbusReadBlock(0x0028, 3, b);

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  if (okA) {
    telemetryCache.voltage = a[0];
    telemetryCache.current = a[1];
  }
  if (okB) {
    telemetryCache.power = b[0];
    telemetryCache.speed = b[1];
    telemetryCache.fault = b[2];
  }
  telemetryCache.valid = okA && okB;
  telemetryCache.updatedMs = millis();
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);

  return okA && okB;
}

static bool execute(const MbRequest& r) {
  switch (r.op) {
  case MbOp::CommSource: return modbusUseCommunicationControl();
  case MbOp::IoSource: return modbusUseIoControl();
  case MbOp::ControlMode: return modbusSetControlMode(r.arg != 0);
  case MbOp::ClosedLoopRpm: return modbusSetClosedLoopRpm(r.arg);
  case MbOp::OpenLoopPercent: return modbusSetOpenLoopSpeedPercent(r.arg);
  case MbOp::AccelMs: return modbusSetAccelTimeMs(r.arg);
  case MbOp::Run: return modbusSetRun(r.arg != 0);
  case MbOp::Direction: return modbusSetDirection(r.arg != 0);
  case MbOp::Brake: return modbusSetBrake(r.arg != 0);
  case MbOp::ClearFault: return modbusClearFault();
  case MbOp::RefreshTelemetry: return refreshTelemetry();
  }
  return false;
}

static void modbusTaskFn(void*) {
  MbRequest r;
  for (;;) {
    // emergency is first priority
    if (xQueueReceive(qUrgent, &r, 0) == pdTRUE) {
      bool ok = execute(r);
      if (r.notify) xTaskNotify(r.notify, ok ? 1u : 0u, eSetValueWithOverwrite);
      continue;
    }

    if (xQueueReceive(qNormal, &r, pdMS_TO_TICKS(5)) == pdTRUE) {
      // ignore everything else while emergency is latched
      if (emergencyLatched) {
        if (r.notify) xTaskNotify(r.notify, 0u, eSetValueWithOverwrite);
        continue;
      }
      bool ok = execute(r);
      if (r.notify) xTaskNotify(r.notify, ok ? 1u : 0u, eSetValueWithOverwrite);
    }
  }
}

static void telemetryTaskFn(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    if (!emergencyLatched) mbCall(MbOp::RefreshTelemetry, 0, pdMS_TO_TICKS(300));
  }
}

static bool submit(QueueHandle_t q, MbOp op, uint16_t arg, TickType_t timeout) {
  if (!q) return false;

  MbRequest r{ op, arg, xTaskGetCurrentTaskHandle() };
  xTaskNotifyStateClear(nullptr);
  if (xQueueSend(q, &r, timeout) != pdTRUE) return false;

  uint32_t value = 0;
  if (xTaskNotifyWait(0, 0xFFFFFFFF, &value, timeout) != pdTRUE) return false;
  return value == 1;
}

bool mbCall(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qNormal, op, arg, timeout);
}

bool mbCallUrgent(MbOp op, uint16_t arg, TickType_t timeout) {
  return submit(qUrgent, op, arg, timeout);
}

void mbSetEmergency(bool latched) {
  emergencyLatched = latched;
}
bool mbIsEmergency() {
  return emergencyLatched;
}

Telemetry mbTelemetry() {
  Telemetry copy;

  if (telemetryMutex) xSemaphoreTake(telemetryMutex, portMAX_DELAY);
  copy = telemetryCache;
  if (telemetryMutex) xSemaphoreGive(telemetryMutex);
  return copy;
}

MbProfile mbProfile() {
  MbProfile p;
  modbusGetStats(p.count, p.timeouts, p.crcErrors, p.lastUs, p.minUs, p.maxUs, p.avgUs);
  return p;
}
void mbProfileReset() {
  modbusResetStats();
}

void modbusTaskBegin() {
  qNormal = xQueueCreate(16, sizeof(MbRequest));
  qUrgent = xQueueCreate(8, sizeof(MbRequest));
  telemetryMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(modbusTaskFn, "modbus", 4096, nullptr, PRIO_MODBUS, nullptr, 1);
  xTaskCreatePinnedToCore(telemetryTaskFn, "telemetry", 4096, nullptr, PRIO_TELEMETRY, nullptr, 1);
}

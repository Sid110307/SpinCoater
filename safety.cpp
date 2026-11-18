#include "safety.h"

#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "hardware.h"
#include "modbus_task.h"
#include "motor.h"
#include "ramp.h"
#include "rtos.h"

static volatile uint32_t holdStartMs = 0;
static volatile bool estopStuck = false;
static volatile bool commsOk = true;
static const char *volatile lastTrip = "";

static void trip(const char *reason) {
  lastTrip = reason;
  rampStop();
  if (!stopMotor()) setEmergencyLatched(true);
}

static float spinRpm() {
  PulseSpeedSnapshot s = getPulseSpeedSnapshot();
  Telemetry t = mbTelemetry();
  float r = s.valid ? s.rpm : 0.0f;
  if (t.valid && t.speed > r) r = t.speed;
  return r;
}

static void superviseHold(uint32_t now, bool running, bool rampActive) {
  if (!running || rampActive) {
    holdStartMs = 0;
    return;
  }
  if (holdStartMs == 0) holdStartMs = now ? now : 1;
  if (config.maxHoldS && now - holdStartMs > static_cast<uint32_t>(config.maxHoldS) * 1000UL) {
    holdStartMs = 0;
    trip("hold_timeout");
  }
}

static void superviseLink(uint32_t now, bool running, bool rampActive) {
  static uint32_t lostSince = 0;
  bool connected = getRuntimeSnapshot().bluetoothConnected;
  if (connected || !running || rampActive || !config.linkStop) {
    lostSince = 0;
    return;
  }
  if (lostSince == 0) lostSince = now ? now : 1;
  if (now - lostSince > LINK_LOSS_STOP_MS) {
    lostSince = 0;
    trip("link_lost");
  }
}

static void superviseDriver(uint32_t now, bool running) {
  static uint32_t badSince = 0;
  Telemetry t = mbTelemetry();
  bool fresh = t.anyOk && (now - t.updatedMs) < COMMS_LOSS_LIMIT_MS;
  commsOk = fresh || !t.updatedMs;
  if (fresh || !running) {
    badSince = 0;
    return;
  }
  if (badSince == 0) badSince = now ? now : 1;
  if (now - badSince > COMMS_LOSS_LIMIT_MS) {
    badSince = 0;
    lastTrip = "driver_silent";
    rampStop();
    setEmergencyLatched(true);
  }
}

static void superviseEstop(uint32_t now, float rpm) {
  static uint32_t lastTryMs = 0, latchedSince = 0;
  static bool runDropped = false;
  if (!isEmergencyLatched()) {
    estopStuck = false;
    latchedSince = 0;
    runDropped = false;
    return;
  }
  if (latchedSince == 0) latchedSince = now ? now : 1;
  bool slow = rpm < SPINNING_RPM;
  bool timedOut = now - latchedSince > BRAKE_STOP_TIMEOUT_MS;
  estopStuck = !slow && timedOut;
  if (runDropped || now - lastTryMs < 250) return;
  lastTryMs = now;
  mbCallUrgent(MbOp::Brake, 1, pdMS_TO_TICKS(120));
  if ((slow || timedOut) && !runDropped)
    runDropped = mbCallUrgent(MbOp::Run, 0, pdMS_TO_TICKS(120));
}

static void safetyTaskFn(void *) {
  esp_task_wdt_add(nullptr);
  bool markedValid = false;
  for (;;) {
    esp_task_wdt_reset();
    uint32_t now = millis();
    bool running = isMotorRunning();
    bool rampActive = rampGetState().active;

    superviseHold(now, running, rampActive);
    superviseLink(now, running, rampActive);
    superviseDriver(now, running);
    float rpm = spinRpm();
    superviseEstop(now, rpm);
    motorServiceBrakeStop(rpm);

    if (!markedValid && now > 15000) {
      esp_ota_mark_app_valid_cancel_rollback();
      markedValid = true;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void safetyBegin() {
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = TASK_WDT_MS;
  cfg.idle_core_mask = 0;
  cfg.trigger_panic = true;
  if (esp_task_wdt_reconfigure(&cfg) != ESP_OK) esp_task_wdt_init(&cfg);
  xTaskCreatePinnedToCore(safetyTaskFn, "safety", 4096, nullptr, PRIO_EMERGENCY - 1, nullptr, 1);
}

uint32_t safetyHoldRemainingS() {
  uint32_t start = holdStartMs;
  if (!start || !config.maxHoldS) return 0;
  uint32_t elapsed = millis() - start;
  uint32_t limit = static_cast<uint32_t>(config.maxHoldS) * 1000UL;
  return elapsed >= limit ? 0 : (limit - elapsed + 999) / 1000;
}

bool safetyEstopStuck() { return estopStuck; }
bool safetyCommsOk() { return commsOk; }
const char *safetyLastTrip() { return lastTrip; }
void safetyClearTrip() { lastTrip = ""; }
void safetyRecordTrip(const char *reason) { lastTrip = reason; }

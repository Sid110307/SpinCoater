#include "hardware.h"

#include "modbus_task.h"
#include "motor.h"
#include "rtos.h"

struct RuntimeState {
  MotorDirection motorDirection = MotorDirection::Clockwise;
  bool motorRunning = false;
  bool emergencyLatched = false;
  bool bluetoothConnected = false;
  bool statusLedReady = false;
};

static constexpr int LEDC_FREQ = 5000;
static constexpr int LEDC_RESOLUTION = 8;

static RuntimeState state;

static bool safetyBypassEnabled() {
  return BYPASS_SAFETY != 0;
}

static bool isPhysicalSafetyInputActive(uint16_t pin) {
  return digitalRead(pin) == LOW;
}

static void writeStatusLed(uint16_t red, uint16_t green, uint16_t blue) {
  if (!state.statusLedReady) return;
  ledcWrite(Pins::LED_R, red);
  ledcWrite(Pins::LED_G, green);
  ledcWrite(Pins::LED_B, blue);
}

static void initializeStatusLed() {
  state.statusLedReady = true;

  ledcAttach(Pins::LED_R, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(Pins::LED_G, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(Pins::LED_B, LEDC_FREQ, LEDC_RESOLUTION);

  writeStatusLed(0, 0, 0);
}

void setupHardware() {
  pinMode(Pins::EMERGENCY, INPUT_PULLUP);
  pinMode(Pins::LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(Pins::BUZZER, OUTPUT);
  digitalWrite(Pins::BUZZER, LOW);
  if (physicalSafetyInputsActive()) setEmergencyLatched(true);

  initializeStatusLed();
  writeStatusLed(255, 0, 0);
  delay(200);
  writeStatusLed(0, 255, 0);
  delay(200);
  writeStatusLed(0, 0, 255);
  delay(200);
  updateStatusLed();
}

RuntimeSnapshot getRuntimeSnapshot() {
  RuntimeSnapshot s;
  stateLock();
  s.direction = state.motorDirection;
  s.motorRunning = state.motorRunning;
  s.emergencyLatched = state.emergencyLatched;
  s.bluetoothConnected = state.bluetoothConnected;
  stateUnlock();
  return s;
}

void setBluetoothConnected(bool connected) {
  stateLock();
  state.bluetoothConnected = connected;
  stateUnlock();
}

bool isBluetoothConnected() {
  stateLock();
  bool v = state.bluetoothConnected;
  stateUnlock();
  return v;
}

void setMotorDirection(MotorDirection direction) {
  stateLock();
  state.motorDirection = direction;
  stateUnlock();
}

MotorDirection getMotorDirection() {
  stateLock();
  MotorDirection v = state.motorDirection;
  stateUnlock();
  return v;
}

void applyMotorRunState(bool running) {
  stateLock();
  state.motorRunning = running;
  stateUnlock();
}

bool isMotorRunning() {
  stateLock();
  bool v = state.motorRunning;
  stateUnlock();
  return v;
}

void setEmergencyLatched(bool latched) {
  if (safetyBypassEnabled()) latched = false;

  stateLock();
  bool changed = state.emergencyLatched != latched;
  state.emergencyLatched = latched;
  stateUnlock();

  mbSetEmergency(latched);
  if (latched && changed) emergencyStopMotor();
}

bool isEmergencyLatched() {
  if (safetyBypassEnabled()) return false;

  stateLock();
  bool v = state.emergencyLatched;
  stateUnlock();
  return v;
}

bool physicalSafetyInputsActive() {
  if (safetyBypassEnabled()) return false;
  return isPhysicalSafetyInputActive(Pins::EMERGENCY) || isPhysicalSafetyInputActive(Pins::LIMIT_SWITCH);
}

bool physicalSafetyInputsInactiveFor(uint32_t stableMs) {
  if (safetyBypassEnabled()) return true;

  uint32_t startMs = millis();
  while ((millis() - startMs) < stableMs) {
    if (physicalSafetyInputsActive()) return false;
    delay(10);
  }

  return !physicalSafetyInputsActive();
}

bool clearEmergencyLatched() {
  if (safetyBypassEnabled()) {
    setEmergencyLatched(false);
    return true;
  }

  if (!physicalSafetyInputsInactiveFor(SAFETY_CLEAR_STABLE_MS)) return false;

  setEmergencyLatched(false);
  return true;
}

MotorStatus getMotorStatus() {
  RuntimeSnapshot s = getRuntimeSnapshot();

  if (s.emergencyLatched) return MotorStatus::EmergencyStop;
  if (!s.motorRunning) return MotorStatus::Idle;
  return s.direction == MotorDirection::Clockwise
    ? MotorStatus::RunningClockwise
    : MotorStatus::RunningCounterClockwise;
}

const char* motorStatusToString(MotorStatus status) {
  switch (status) {
  case MotorStatus::Idle: return "idle";
  case MotorStatus::RunningClockwise: return "running_cw";
  case MotorStatus::RunningCounterClockwise: return "running_ccw";
  case MotorStatus::EmergencyStop: return "emergency_stop";
  default: return "unknown";
  }
}

void updateStatusLed() {
  RuntimeSnapshot s = getRuntimeSnapshot();
  MotorStatus status = MotorStatus::Idle;
  if (s.emergencyLatched)
    status = MotorStatus::EmergencyStop;
  else if (s.motorRunning)
    status = s.direction == MotorDirection::Clockwise
    ? MotorStatus::RunningClockwise
    : MotorStatus::RunningCounterClockwise;

  switch (status) {
  case MotorStatus::EmergencyStop:
    writeStatusLed(255, 0, 0);
    break;
  case MotorStatus::RunningClockwise:
  case MotorStatus::RunningCounterClockwise:
    writeStatusLed(0, 255, 0);
    break;
  case MotorStatus::Idle:
  default:
    if (s.bluetoothConnected)
      writeStatusLed(0, 192, 192);
    else
      writeStatusLed(255, 32, 0);
    break;
  }
}

// Wake emergency task from ISR
void IRAM_ATTR handleEmergencyInterrupt() {
  BaseType_t hpw = pdFALSE;
  if (gEmergencyTaskHandle) vTaskNotifyGiveFromISR(gEmergencyTaskHandle, &hpw);
  if (hpw) portYIELD_FROM_ISR();
}

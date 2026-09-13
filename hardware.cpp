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
static constexpr int LEDC_CHANNEL_R = 0;
static constexpr int LEDC_CHANNEL_G = 1;
static constexpr int LEDC_CHANNEL_B = 2;

static RuntimeState state;

static portMUX_TYPE speedPulseMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t speedLastEdgeUs = 0;
static volatile uint32_t speedLastPeriodUs = 0;
static volatile uint32_t speedPulseCount = 0;
static volatile uint32_t speedPeriods[SPEED_SAMPLE_WINDOW] = {};
static volatile uint8_t speedPeriodIndex = 0;
static volatile bool speedPeriodsFilled = false;

static MotorStatus motorStatusFromSnapshot(const RuntimeSnapshot &snapshot) {
  if (snapshot.emergencyLatched) return MotorStatus::EmergencyStop;
  if (!snapshot.motorRunning) return MotorStatus::Idle;
  return snapshot.direction == MotorDirection::Clockwise
    ? MotorStatus::RunningClockwise
    : MotorStatus::RunningCounterClockwise;
}

static bool isPhysicalSafetyInputActive(uint16_t pin) {
  return digitalRead(pin) == LOW;
}

static void writeStatusLedChannel(uint16_t pin, int channel, uint16_t duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, duty);
#else
  ledcWrite(channel, duty);
#endif
}

static void writeStatusLed(uint16_t red, uint16_t green, uint16_t blue) {
  if (!state.statusLedReady) return;
  writeStatusLedChannel(Pins::LED_R, LEDC_CHANNEL_R, red);
  writeStatusLedChannel(Pins::LED_G, LEDC_CHANNEL_G, green);
  writeStatusLedChannel(Pins::LED_B, LEDC_CHANNEL_B, blue);
}

static void initializeStatusLed() {
  state.statusLedReady = true;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(Pins::LED_R, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(Pins::LED_G, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttach(Pins::LED_B, LEDC_FREQ, LEDC_RESOLUTION);
#else
  ledcSetup(LEDC_CHANNEL_R, LEDC_FREQ, LEDC_RESOLUTION);
  ledcSetup(LEDC_CHANNEL_G, LEDC_FREQ, LEDC_RESOLUTION);
  ledcSetup(LEDC_CHANNEL_B, LEDC_FREQ, LEDC_RESOLUTION);
  ledcAttachPin(Pins::LED_R, LEDC_CHANNEL_R);
  ledcAttachPin(Pins::LED_G, LEDC_CHANNEL_G);
  ledcAttachPin(Pins::LED_B, LEDC_CHANNEL_B);
#endif

  writeStatusLed(0, 0, 0);
}

void setupHardware() {
  pinMode(Pins::EMERGENCY, INPUT_PULLUP);
  pinMode(Pins::LIMIT_SWITCH, INPUT_PULLUP);
  pinMode(Pins::SPEED, INPUT);
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
  updateStatusIndicators();
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

static PulseSpeedSnapshot getPulseSpeedSnapshot(uint8_t maximumPeriods) {
  uint32_t periods[SPEED_SAMPLE_WINDOW];
  uint32_t lastEdgeUs;
  uint32_t lastPeriodUs;
  uint32_t pulseCount;
  uint8_t periodIndex;
  bool periodsFilled;

  portENTER_CRITICAL(&speedPulseMux);
  lastEdgeUs = speedLastEdgeUs;
  lastPeriodUs = speedLastPeriodUs;
  pulseCount = speedPulseCount;
  periodIndex = speedPeriodIndex;
  periodsFilled = speedPeriodsFilled;
  for (uint8_t i = 0; i < SPEED_SAMPLE_WINDOW; ++i) periods[i] = speedPeriods[i];
  portEXIT_CRITICAL(&speedPulseMux);

  PulseSpeedSnapshot result{};
  result.pulseCount = pulseCount;
  result.lastEdgeUs = lastEdgeUs;
  result.lastPeriodUs = lastPeriodUs;
  if (lastEdgeUs == 0 || lastPeriodUs == 0) return result;

  uint32_t ageUs = micros() - lastEdgeUs;
  result.ageMs = ageUs / 1000;
  if (ageUs > SPEED_STALE_TIMEOUT_MS * 1000UL) return result;

  uint8_t availablePeriods = periodsFilled ? SPEED_SAMPLE_WINDOW : periodIndex;
  uint8_t sampleCount = min(availablePeriods, maximumPeriods);
  if (sampleCount == 0) return result;

  uint64_t sumUs = 0;
  uint32_t minUs = UINT32_MAX;
  uint32_t maxUs = 0;

  for (uint8_t i = 0; i < sampleCount; ++i) {
    uint8_t index = (periodIndex + SPEED_SAMPLE_WINDOW - 1 - i) % SPEED_SAMPLE_WINDOW;
    uint32_t periodUs = periods[index];
    sumUs += periodUs;
    if (periodUs < minUs) minUs = periodUs;
    if (periodUs > maxUs) maxUs = periodUs;
  }

  result.minPeriodUs = minUs;
  result.maxPeriodUs = maxUs;
  result.jitterUs = maxUs - minUs;
  result.averagePeriodUs = static_cast<float>(sumUs) / sampleCount;
  result.frequencyHz = TIMER_FREQUENCY / result.averagePeriodUs;
  result.rpm = result.frequencyHz * 60.0f / (3.0f * SPEED_POLE_PAIRS);
  result.valid = true;

  return result;
}

PulseSpeedSnapshot getPulseSpeedSnapshot() {
  return getPulseSpeedSnapshot(SPEED_SAMPLE_WINDOW);
}

PulseSpeedSnapshot getProfilePulseSpeedSnapshot() {
  return getPulseSpeedSnapshot(PROFILE_SPEED_AVERAGE_PERIODS);
}

void setBluetoothConnected(bool connected) {
  stateLock();
  state.bluetoothConnected = connected;
  stateUnlock();
}

void setMotorDirection(MotorDirection direction) {
  stateLock();
  state.motorDirection = direction;
  stateUnlock();
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
  return motorStatusFromSnapshot(getRuntimeSnapshot());
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

void updateStatusIndicators() {
  RuntimeSnapshot s = getRuntimeSnapshot();
  MotorStatus status = motorStatusFromSnapshot(s);

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

  uint32_t phase;
  bool buzzerOn = false;
  if (status == MotorStatus::EmergencyStop) {
    phase = millis() % 1500;
    buzzerOn = phase < 120 || (phase >= 240 && phase < 360) ||
               (phase >= 480 && phase < 600);
  } else {
    Telemetry telemetry = mbTelemetry();
    if (telemetry.fault != 0) {
      phase = millis() % 2000;
      buzzerOn = phase < 600;
    }
  }
  digitalWrite(Pins::BUZZER, buzzerOn ? HIGH : LOW);
}

// Wake emergency task from ISR
void IRAM_ATTR handleEmergencyInterrupt() {
  BaseType_t hpw = pdFALSE;
  if (gEmergencyTaskHandle) vTaskNotifyGiveFromISR(gEmergencyTaskHandle, &hpw);
  if (hpw) portYIELD_FROM_ISR();
}

void IRAM_ATTR handleSpeedPulseInterrupt() {
  uint32_t nowUs = micros();

  portENTER_CRITICAL_ISR(&speedPulseMux);
  if (speedLastEdgeUs == 0) {
    speedLastEdgeUs = nowUs;
    ++speedPulseCount;
    portEXIT_CRITICAL_ISR(&speedPulseMux);
    return;
  }

  uint32_t periodUs = nowUs - speedLastEdgeUs;
  if (periodUs > SPEED_STALE_TIMEOUT_MS * 1000UL) {
    speedLastEdgeUs = nowUs;
    speedLastPeriodUs = 0;
    speedPeriodIndex = 0;
    speedPeriodsFilled = false;
    ++speedPulseCount;
    portEXIT_CRITICAL_ISR(&speedPulseMux);
    return;
  }
  if (periodUs >= SPEED_GLITCH_REJECT_US) {
    speedLastEdgeUs = nowUs;
    speedLastPeriodUs = periodUs;
    speedPeriods[speedPeriodIndex] = periodUs;
    speedPeriodIndex = (speedPeriodIndex + 1) % SPEED_SAMPLE_WINDOW;
    if (speedPeriodIndex == 0) speedPeriodsFilled = true;
    ++speedPulseCount;
  }
  portEXIT_CRITICAL_ISR(&speedPulseMux);
}

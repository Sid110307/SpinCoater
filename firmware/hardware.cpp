#include "hardware.h"

#include "motor.h"

struct RuntimeState {
	volatile bool emergencyInterruptPending = false;
	volatile bool alarmInterruptPending = false;
	volatile uint32_t speedPulseCount = 0;

	MotorDirection motorDirection = MotorDirection::Clockwise;
	bool motorRunning = false;
	bool emergencyLatched = false;
	bool alarmLatched = false;
	bool bluetoothConnected = false;
	bool statusLedReady = false;
	float lastMeasuredRpm = 0.0f;
};

static constexpr int LEDC_FREQ = 5000;
static constexpr int LEDC_RESOLUTION = 8;

static RuntimeState state;
static unsigned long lastEmergencyMillis = 0;

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
	pinMode(Pins::ALM, INPUT_PULLUP);
	pinMode(Pins::SPEED, INPUT_PULLUP);
	pinMode(Pins::EMERGENCY, INPUT_PULLUP);

	initializeStatusLed();
	writeStatusLed(255, 0, 0);
	delay(750);
	writeStatusLed(0, 255, 0);
	delay(750);
	writeStatusLed(0, 0, 255);
	delay(750);

	updateStatusLed();
}

void serviceHardware() {
	bool emergencyPending = false;
	bool alarmPending = false;

	noInterrupts();
	emergencyPending = state.emergencyInterruptPending;
	alarmPending = state.alarmInterruptPending;
	state.emergencyInterruptPending = false;
	state.alarmInterruptPending = false;
	interrupts();

	if (emergencyPending) {
		unsigned long now = millis();
		if (now - lastEmergencyMillis >= DEBOUNCE_MS) {
			lastEmergencyMillis = now;
			setEmergencyLatched(true);
		}
	}

	if (alarmPending) setAlarmLatched(true);
	updateStatusLed();
}

void setBluetoothConnected(bool connected) {
	if (state.bluetoothConnected == connected) return;

	state.bluetoothConnected = connected;
	updateStatusLed();
}

bool isBluetoothConnected() {
	return state.bluetoothConnected;
}

void setMotorDirection(MotorDirection direction) {
	state.motorDirection = direction;
	updateStatusLed();
}

MotorDirection getMotorDirection() {
	return state.motorDirection;
}

void applyMotorRunState(bool running) {
	state.motorRunning = running;
	updateStatusLed();
}

bool isMotorRunning() {
	return state.motorRunning;
}

void setMotorPolePairs(int polePairs) {
	if (polePairs > 0) config.speedPulsesPerRevolution = static_cast<uint16_t>(polePairs);
}

int getMotorPolePairs() {
	return config.speedPulsesPerRevolution > 0 ? config.speedPulsesPerRevolution : DEFAULT_SPEED_PULSES_PER_REVOLUTION;
}

void setEmergencyLatched(bool latched) {
	if (state.emergencyLatched == latched) return;

	state.emergencyLatched = latched;
	if (latched) {
		applyMotorRunState(false);
		stopMotor();
	}
	updateStatusLed();
}

bool isEmergencyLatched() {
	return state.emergencyLatched;
}

void clearEmergencyLatched() {
	setEmergencyLatched(false);
}

void setAlarmLatched(bool latched) {
	if (state.alarmLatched == latched) return;

	state.alarmLatched = latched;
	if (latched) {
		applyMotorRunState(false);
		stopMotor();
	}
	updateStatusLed();
}

bool isAlarmLatched() {
	return state.alarmLatched;
}

void clearAlarmLatched() {
	setAlarmLatched(false);
}

MotorStatus getMotorStatus() {
	if (state.emergencyLatched) return MotorStatus::EmergencyStop;
	if (state.alarmLatched) return MotorStatus::AlarmFault;
	if (!state.motorRunning) return MotorStatus::Idle;
	return state.motorDirection == MotorDirection::Clockwise ? MotorStatus::RunningClockwise : MotorStatus::RunningCounterClockwise;
}

const char* motorStatusToString(MotorStatus status) {
	switch (status) {
	case MotorStatus::Idle:
		return "idle";
	case MotorStatus::RunningClockwise:
		return "running_cw";
	case MotorStatus::RunningCounterClockwise:
		return "running_ccw";
	case MotorStatus::EmergencyStop:
		return "emergency_stop";
	case MotorStatus::AlarmFault:
		return "alarm_fault";
	default:
		return "unknown";
	}
}

void updateStatusLed() {
	switch (getMotorStatus()) {
	case MotorStatus::EmergencyStop:
		writeStatusLed(255, 0, 0);
		break;
	case MotorStatus::RunningClockwise:
	case MotorStatus::RunningCounterClockwise:
		writeStatusLed(0, 255, 0);
		break;
	case MotorStatus::Idle:
	default:
		if (state.bluetoothConnected) writeStatusLed(0, 192, 192);
		else writeStatusLed(255, 32, 0);

		break;
	}
}

uint32_t getSpeedPulseCount() {
	noInterrupts();
	uint32_t count = state.speedPulseCount;
	interrupts();

	return count;
}

void resetSpeedPulseCount() {
	noInterrupts();
	state.speedPulseCount = 0;
	interrupts();
}

float sampleSpeedRpm() {
	uint32_t pulseCount = 0;

	noInterrupts();
	pulseCount = state.speedPulseCount;
	state.speedPulseCount = 0;
	interrupts();

	if (!state.motorRunning || state.emergencyLatched || state.alarmLatched) {
		state.lastMeasuredRpm = 0.0f;
		return state.lastMeasuredRpm;
	}

	state.lastMeasuredRpm = (static_cast<float>(pulseCount) * 60.0f) / static_cast<float>(getMotorPolePairs());
	return state.lastMeasuredRpm;
}

float getLastMeasuredSpeedRpm() {
	return state.lastMeasuredRpm;
}

void IRAM_ATTR handleEmergencyInterrupt() {
	state.emergencyInterruptPending = true;
}

void IRAM_ATTR handleAlarmInterrupt() {
	state.alarmInterruptPending = true;
}

void IRAM_ATTR handleSpeedInterrupt() {
	++state.speedPulseCount;
}
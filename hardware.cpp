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

static RuntimeState state;
static unsigned long lastEmergencyMillis = 0;

static void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
	if (!state.statusLedReady) return;

	digitalWrite(Pins::LED_R, red ? HIGH : LOW);
	digitalWrite(Pins::LED_G, green ? HIGH : LOW);
	digitalWrite(Pins::LED_B, blue ? HIGH : LOW);
}

static void initializeStatusLed() {
	state.statusLedReady = true;
	if (state.statusLedReady) writeStatusLed(0, 0, 0);
}

bool isValidMotorPwmPin(int pin) {
	switch (pin) {
	case 13:
	case 16:
	case 17:
	case 22:
	case 32:
		return true;
	default:
		return false;
	}
}

void setupHardware() {
	pinMode(Pins::EN, OUTPUT);
	digitalWrite(Pins::EN, HIGH);

	pinMode(Pins::BRK, OUTPUT);
	digitalWrite(Pins::BRK, HIGH);

	pinMode(Pins::DIR, OUTPUT);
	digitalWrite(Pins::DIR, HIGH);

	pinMode(Pins::LED_R, OUTPUT);
	pinMode(Pins::LED_G, OUTPUT);
	pinMode(Pins::LED_B, OUTPUT);

	pinMode(Pins::ALM, INPUT_PULLUP);
	pinMode(Pins::SPEED, INPUT_PULLUP);
	pinMode(Pins::EMERGENCY, INPUT_PULLUP);

	initializeStatusLed();
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

	if (alarmPending) {
		setAlarmLatched(true);
	}

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
	digitalWrite(Pins::DIR, direction == MotorDirection::Clockwise ? HIGH : LOW);
	updateStatusLed();
}

MotorDirection getMotorDirection() {
	return state.motorDirection;
}

void applyMotorRunState(bool running) {
	state.motorRunning = running;

	// The EN pin drives a 2N2222 stage, so the MCU signal is intentionally
	// inverted from the downstream motor-driver enable behavior
	digitalWrite(Pins::EN, running ? LOW : HIGH);
	digitalWrite(Pins::BRK, running ? LOW : HIGH);

	updateStatusLed();
}

bool isMotorRunning() {
	return state.motorRunning;
}

void setMotorPolePairs(int polePairs) {
	if (polePairs > 0) {
		config.polePairs = polePairs;
	}
}

int getMotorPolePairs() {
	return config.polePairs > 0 ? config.polePairs : DEFAULT_POLE_PAIRS;
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
	case MotorStatus::AlarmFault:
		writeStatusLed(255, 96, 0);
		break;
	case MotorStatus::RunningClockwise:
	case MotorStatus::RunningCounterClockwise:
		writeStatusLed(0, 255, 0);
		break;
	case MotorStatus::Idle:
	default:
		if (state.bluetoothConnected) writeStatusLed(0, 180, 180);
		else writeStatusLed(180, 180, 0);

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
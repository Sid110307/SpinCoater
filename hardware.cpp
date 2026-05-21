#include "hardware.h"

#include "motor.h"

static volatile bool gEmergencyInterruptPending = false;
static volatile bool gAlarmInterruptPending = false;
static volatile uint32_t gSpeedPulseCount = 0;

static MotorDirection gMotorDirection = MotorDirection::Clockwise;
static bool gMotorRunning = false;
static bool gEmergencyLatched = false;
static bool gAlarmLatched = false;
static bool gBluetoothConnected = false;
static bool gStatusLedReady = false;
static float gLastMeasuredRpm = 0.0f;

static void writeStatusLed(uint8_t red, uint8_t green, uint8_t blue) {
	if (!gStatusLedReady) return;

	digitalWrite(Pins::LED_R, red ? HIGH : LOW);
	digitalWrite(Pins::LED_G, green ? HIGH : LOW);
	digitalWrite(Pins::LED_B, blue ? HIGH : LOW);
}

static void initializeStatusLed() {
	gStatusLedReady = true;

	if (gStatusLedReady) {
		writeStatusLed(0, 0, 0);
	}
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
	emergencyPending = gEmergencyInterruptPending;
	alarmPending = gAlarmInterruptPending;
	gEmergencyInterruptPending = false;
	gAlarmInterruptPending = false;
	interrupts();

	if (emergencyPending) {
		setEmergencyLatched(true);
	}

	if (alarmPending) {
		setAlarmLatched(true);
	}

	updateStatusLed();
}

void setBluetoothConnected(bool connected) {
	if (gBluetoothConnected == connected) return;

	gBluetoothConnected = connected;
	updateStatusLed();
}

bool isBluetoothConnected() {
	return gBluetoothConnected;
}

void setMotorDirection(MotorDirection direction) {
	gMotorDirection = direction;
	digitalWrite(Pins::DIR, direction == MotorDirection::Clockwise ? HIGH : LOW);
	updateStatusLed();
}

MotorDirection getMotorDirection() {
	return gMotorDirection;
}

void applyMotorRunState(bool running) {
	gMotorRunning = running;

	// The EN pin drives a 2N2222 stage, so the MCU signal is intentionally
	// inverted from the downstream motor-driver enable behavior.
	digitalWrite(Pins::EN, running ? LOW : HIGH);
	digitalWrite(Pins::BRK, running ? LOW : HIGH);

	updateStatusLed();
}

bool isMotorRunning() {
	return gMotorRunning;
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
	if (gEmergencyLatched == latched) return;

	gEmergencyLatched = latched;
	if (latched) {
		applyMotorRunState(false);
		stopMotor();
	}
	updateStatusLed();
}

bool isEmergencyLatched() {
	return gEmergencyLatched;
}

void clearEmergencyLatched() {
	setEmergencyLatched(false);
}

void setAlarmLatched(bool latched) {
	if (gAlarmLatched == latched) return;

	gAlarmLatched = latched;
	if (latched) {
		applyMotorRunState(false);
		stopMotor();
	}
	updateStatusLed();
}

bool isAlarmLatched() {
	return gAlarmLatched;
}

void clearAlarmLatched() {
	setAlarmLatched(false);
}

MotorStatus getMotorStatus() {
	if (gEmergencyLatched) return MotorStatus::EmergencyStop;
	if (gAlarmLatched) return MotorStatus::AlarmFault;
	if (!gMotorRunning) return MotorStatus::Idle;
	return gMotorDirection == MotorDirection::Clockwise ? MotorStatus::RunningClockwise : MotorStatus::RunningCounterClockwise;
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
			writeStatusLed(0, 255, 0);
			break;
		case MotorStatus::RunningCounterClockwise:
			writeStatusLed(0, 0, 255);
			break;
		case MotorStatus::Idle:
		default:
			if (gBluetoothConnected) {
				writeStatusLed(0, 180, 180);
			} else {
				writeStatusLed(0, 0, 0);
			}
			break;
	}
}

uint32_t getSpeedPulseCount() {
	noInterrupts();
	uint32_t count = gSpeedPulseCount;
	interrupts();
	return count;
}

void resetSpeedPulseCount() {
	noInterrupts();
	gSpeedPulseCount = 0;
	interrupts();
}

float sampleSpeedRpm() {
	uint32_t pulseCount = 0;

	noInterrupts();
	pulseCount = gSpeedPulseCount;
	gSpeedPulseCount = 0;
	interrupts();

	if (!gMotorRunning || gEmergencyLatched || gAlarmLatched) {
		gLastMeasuredRpm = 0.0f;
		return gLastMeasuredRpm;
	}

	gLastMeasuredRpm = (static_cast<float>(pulseCount) * 60.0f) / static_cast<float>(getMotorPolePairs());
	return gLastMeasuredRpm;
}

float getLastMeasuredSpeedRpm() {
	return gLastMeasuredRpm;
}

void IRAM_ATTR handleEmergencyInterrupt() {
	gEmergencyInterruptPending = true;
}

void IRAM_ATTR handleAlarmInterrupt() {
	gAlarmInterruptPending = true;
}

void IRAM_ATTR handleSpeedInterrupt() {
	++gSpeedPulseCount;
}
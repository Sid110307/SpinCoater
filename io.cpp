#include "io.h"
#include "globals.h"
#include "modbus.h"

static const char* controlModeToString(ControlMode controlMode) {
	switch (controlMode) {
	case ControlMode::OpenLoop:
		return "open_loop";
	case ControlMode::ClosedLoop:
		return "closed_loop";
	default:
		return "unknown";
	}
}

static void addPinMap(JsonObject pins) {
	pins["emergency"] = Pins::EMERGENCY;
	pins["speed"] = Pins::SPEED;
	pins["rs485_rx"] = Pins::RS485_RX;
	pins["rs485_tx"] = Pins::RS485_TX;
	pins["rs485_de_re"] = Pins::RS485_DE_RE;
	pins["led_r"] = Pins::LED_R;
	pins["led_g"] = Pins::LED_G;
	pins["led_b"] = Pins::LED_B;
}

String getJSONString() {
	JsonDocument doc;
	uint16_t motorVoltage = 0;
	uint16_t motorCurrent = 0;
	uint16_t motorPower = 0;
	uint16_t motorSpeed = 0;
	uint16_t motorFault = 0;

	modbusReadVoltage(motorVoltage);
	modbusReadCurrent(motorCurrent);
	modbusReadPower(motorPower);
	modbusReadSpeed(motorSpeed);
	modbusReadFault(motorFault);

	doc["version"] = config.version;
	doc["modbus_address"] = config.modbusAddress;
	doc["control_mode"] = controlModeToString(config.controlMode);
	doc["direction"] = config.direction == MotorDirection::Clockwise ? "cw" : "ccw";
	doc["target_rpm"] = config.targetRpm;
	doc["open_loop_raw"] = config.openLoopCommandRaw;
	doc["accel_ms"] = config.accelerationMs;
	doc["speed_pulses_per_revolution"] = config.speedPulsesPerRevolution;
	doc["status"] = motorStatusToString(getMotorStatus());
	doc["running"] = isMotorRunning();
	doc["emergency"] = isEmergencyLatched();
	doc["bt_connected"] = isBluetoothConnected();
	doc["speed_pulses"] = getSpeedPulseCount();
	doc["speed_rpm"] = getLastMeasuredSpeedRpm();
	doc["motor_voltage"] = motorVoltage;
	doc["motor_current"] = static_cast<float>(motorCurrent) / 100.0f;
	doc["motor_power"] = static_cast<float>(motorPower) / 10.0f;
	doc["motor_speed"] = motorSpeed;
	doc["motor_fault"] = motorFault;
	addPinMap(doc["pins"].to<JsonObject>());

	String output;
	serializeJson(doc, output);
	return output;
}

void getJSON(Stream& stream) {
	stream.print(getJSONString());
}

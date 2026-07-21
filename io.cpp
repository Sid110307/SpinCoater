#include "io.h"
#include "globals.h"
#include "modbus.h"

static void addPinMap(JsonObject pins) {
	pins["pwm"] = Pins::PWM;
	pins["alm"] = Pins::ALM;
	pins["speed"] = Pins::SPEED;
	pins["brk"] = Pins::BRK;
	pins["dir"] = Pins::DIR;
	pins["en"] = Pins::EN;
	pins["emergency"] = Pins::EMERGENCY;
	pins["led_r"] = Pins::LED_R;
	pins["led_g"] = Pins::LED_G;
	pins["led_b"] = Pins::LED_B;
}

void getPasses(JsonArray passes) {
	for (int i = 0; i < config.numPasses; ++i) {
		JsonObject pass = passes.add<JsonObject>();

		pass["rpm"] = config.passes[i].rpm;
		pass["accel"] = config.passes[i].accel;
		pass["delay"] = config.passes[i].delay;
	}
}

void getPasses(Stream& stream) {
	JsonDocument doc;

	getPasses(doc.to<JsonArray>());
	serializeJson(doc, stream);
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

	doc["freq"] = config.frequency;
	doc["duty"] = config.dutyCycle;
	doc["move"] = config.length;
	doc["length_mm"] = config.length;
	doc["pwmdur"] = config.duration;
	doc["duration_ms"] = config.duration;
	doc["pwmpin"] = config.pwmPin;
	doc["status"] = motorStatusToString(getMotorStatus());
	doc["direction"] = getMotorDirection() == MotorDirection::Clockwise ? "cw" : "ccw";
	doc["running"] = isMotorRunning();
	doc["emergency"] = isEmergencyLatched();
	doc["alarm"] = isAlarmLatched();
	doc["bt_connected"] = isBluetoothConnected();
	doc["pole_pairs"] = getMotorPolePairs();
	doc["speed_pulses"] = getSpeedPulseCount();
	doc["speed_rpm"] = getLastMeasuredSpeedRpm();
	doc["motor_voltage"] = motorVoltage;
	doc["motor_current"] = static_cast<float>(motorCurrent) / 100.0f;
	doc["motor_power"] = static_cast<float>(motorPower) / 10.0f;
	doc["motor_speed"] = motorSpeed;
	doc["motor_fault"] = motorFault;

	addPinMap(doc["pins"].to<JsonObject>());
	getPasses(doc["passes"].to<JsonArray>());

	String output;
	serializeJson(doc, output);
	return output;
}

void getJSON(Stream& stream) {
	stream.print(getJSONString());
}

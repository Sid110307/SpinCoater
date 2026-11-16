#include "commands.h"
#include "io.h"
#include "motor.h"
#include "modbus.h"
#include "globals.h"

#include <cstdlib>

static constexpr char RESP_OK[] = "OK";

static String makeError(const char* code, const char* field = nullptr) {
  if (field && field[0] != '\0') {
    return String("{\"error\":\"") + code + String("\",\"field\":\"") + field + String("\"}");
  }
  return String("{\"error\":\"") + code + String("\"}");
}

static String printJSON() {
  String json = getJSONString();
  Serial.println(json);

  return json;
}

String parse(String input) {
  if (input.length() == 0) return makeError("INVALID_CMD");
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("start")) {
      if (!startMotor()) return makeError("SAFETY_LOCKED");
      return RESP_OK;
    }
    else if (input.equalsIgnoreCase("stop")) {
      stopMotor();
      return RESP_OK;
    }
    else if (input.equalsIgnoreCase("status")) {
      return printJSON();
    }
    else if (input.equalsIgnoreCase("estop")) {
      setEmergencyLatched(true);
      return RESP_OK;
    }
    else if (input.equalsIgnoreCase("clearfault")) {
      clearEmergencyLatched();
      clearAlarmLatched();
      modbusClearFault();
      stopMotor();

      return RESP_OK;
    }
    return makeError("INVALID_CMD");
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("rpm")) {
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0) return makeError("INVALID_VALUE", "rpm");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "rpm");

    uint16_t rpm = static_cast<uint16_t>(v);

    if (!changeTargetRpm(rpm)) return makeError("INVALID_VALUE", "rpm");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("open_loop_raw")) {
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0) return makeError("INVALID_VALUE", "open_loop_raw");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "open_loop_raw");

    uint16_t raw = static_cast<uint16_t>(v);

    if (!changeOpenLoopCommandRaw(raw)) return makeError("INVALID_VALUE", "open_loop_raw");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("accel_ms")) {
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0) return makeError("INVALID_VALUE", "accel_ms");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "accel_ms");

    uint16_t accelMs = static_cast<uint16_t>(v);

    if (!changeAccelerationMs(accelMs)) return makeError("INVALID_VALUE", "accel_ms");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("speed_pulses_per_revolution")) {
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0) return makeError("INVALID_VALUE", "speed_pulses_per_revolution");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "speed_pulses_per_revolution");

    uint16_t pulses = static_cast<uint16_t>(v);

    if (!changeSpeedPulsesPerRevolution(pulses)) return makeError("INVALID_VALUE", "speed_pulses_per_revolution");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("dir")) {
    if (value.equalsIgnoreCase("1") || value.equalsIgnoreCase("cw") || value.equalsIgnoreCase("clockwise")) {
      if (!changeDirection(MotorDirection::Clockwise)) return makeError("INVALID_VALUE", "dir");
      return RESP_OK;
    }
    else if (value.equalsIgnoreCase("0") || value.equalsIgnoreCase("ccw") || value.equalsIgnoreCase("counterclockwise")) {
      if (!changeDirection(MotorDirection::CounterClockwise)) return makeError("INVALID_VALUE", "dir");
      return RESP_OK;
    }

    return makeError("INVALID_VALUE", "dir");
  }
  else if (command.equalsIgnoreCase("mode")) {
    if (value.equalsIgnoreCase("open_loop")) {
      if (!changeControlMode(ControlMode::OpenLoop)) return makeError("INVALID_VALUE", "mode");
      return RESP_OK;
    }
    else if (value.equalsIgnoreCase("closed_loop")) {
      if (!changeControlMode(ControlMode::ClosedLoop)) return makeError("INVALID_VALUE", "mode");
      return RESP_OK;
    }

    return makeError("INVALID_VALUE", "mode");
  }
  else return makeError("INVALID_CMD");
}

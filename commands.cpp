#include "commands.h"
#include "globals.h"
#include "io.h"
#include "modbus_task.h"
#include "motor.h"
#include "ramp.h"

#include <cstdlib>

static constexpr char RESP_OK[] = "OK";

static String makeError(const char *code, const char *field = nullptr) {
  if (field && field[0] != '\0')
    return String("{\"error\":\"") + code + String("\",\"field\":\"") + field + String("\"}");
  return String("{\"error\":\"") + code + String("\"}");
}

static String printJSON() {
  String json = getJSONString();
  Serial.println(json);

  return json;
}

static String profileJSON() {
  MbProfile p = mbProfile();
  JsonDocument d;
  d["count"] = p.count;
  d["timeouts"] = p.timeouts;
  d["crc_errors"] = p.crcErrors;
  d["last_us"] = p.lastUs;
  d["min_us"] = p.minUs;
  d["max_us"] = p.maxUs;
  d["avg_us"] = p.avgUs;

  String o;
  serializeJson(d, o);
  return o;
}

String parse(String input) {
  if (input.length() == 0) return makeError("INVALID_CMD", "Empty command");
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("start")) {
      if (!startMotor()) return makeError("SAFETY_LOCKED");
      return RESP_OK;
    } else if (input.equalsIgnoreCase("stop")) {
      rampStop();
      stopMotor();
      return RESP_OK;
    } else if (input.equalsIgnoreCase("status")) {
      return printJSON();
    } else if (input.equalsIgnoreCase("estop")) {
      rampStop();
      setEmergencyLatched(true);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("clearfault")) {
      if (!clearEmergencyLatched()) return makeError("SAFETY_LOCKED");
      mbCall(MbOp::ClearFault);
      stopMotor();

      return RESP_OK;
    } else if (input.equalsIgnoreCase("rampstop")) {
      rampStop();
      return RESP_OK;
    } else if (input.equalsIgnoreCase("rampclear")) {
      rampBuilderReset(0);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("rampstart")) {
      if (!rampStart()) return makeError("SAFETY_LOCKED");
      return RESP_OK;
    } else if (input.equalsIgnoreCase("profile")) {
      return profileJSON();
    } else if (input.equalsIgnoreCase("profilereset")) {
      mbProfileReset();
      return RESP_OK;
    }
    return makeError("INVALID_CMD", input.c_str());
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("rpm")) {
    char *endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0)
      return makeError("INVALID_VALUE", "rpm");
    while (*endptr && isspace(*endptr))
      ++endptr;
    if (*endptr)
      return makeError("INVALID_VALUE", "rpm");

    uint16_t rpm = static_cast<uint16_t>(v);

    if (!changeTargetRpm(rpm))
      return makeError("INVALID_VALUE", "rpm");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("open_loop_raw")) {
    char *endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0)
      return makeError("INVALID_VALUE", "open_loop_raw");
    while (*endptr && isspace(*endptr))
      ++endptr;
    if (*endptr)
      return makeError("INVALID_VALUE", "open_loop_raw");

    uint16_t raw = static_cast<uint16_t>(v);

    if (!changeOpenLoopCommandRaw(raw))
      return makeError("INVALID_VALUE", "open_loop_raw");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("accel_ms")) {
    char *endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0)
      return makeError("INVALID_VALUE", "accel_ms");
    while (*endptr && isspace(*endptr))
      ++endptr;
    if (*endptr)
      return makeError("INVALID_VALUE", "accel_ms");

    uint16_t accelMs = static_cast<uint16_t>(v);

    if (!changeAccelerationMs(accelMs))
      return makeError("INVALID_VALUE", "accel_ms");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("dir")) {
    if (value.equalsIgnoreCase("1") || value.equalsIgnoreCase("cw") || value.equalsIgnoreCase("clockwise")) {
      if (!changeDirection(MotorDirection::Clockwise))
        return makeError("INVALID_VALUE", "dir");
      return RESP_OK;
    } else if (value.equalsIgnoreCase("0") || value.equalsIgnoreCase("ccw") || value.equalsIgnoreCase("counterclockwise")) {
      if (!changeDirection(MotorDirection::CounterClockwise))
        return makeError("INVALID_VALUE", "dir");
      return RESP_OK;
    }

    return makeError("INVALID_VALUE", "dir");
  } else if (command.equalsIgnoreCase("mode")) {
    if (value.equalsIgnoreCase("open_loop")) {
      if (!changeControlMode(ControlMode::OpenLoop))
        return makeError("INVALID_VALUE", "mode");
      return RESP_OK;
    } else if (value.equalsIgnoreCase("closed_loop")) {
      if (!changeControlMode(ControlMode::ClosedLoop))
        return makeError("INVALID_VALUE", "mode");
      return RESP_OK;
    }

    return makeError("INVALID_VALUE", "mode");
  } else if (command.equalsIgnoreCase("ramp")) {
    const char *errField = nullptr;
    if (!rampParseLoadStart(value.c_str(), &errField))
      return makeError("INVALID_VALUE", errField ? errField : "ramp");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("rampload")) {
    const char *errField = nullptr;
    RampProfile profile;
    if (!rampParse(value.c_str(), profile, &errField))
      return makeError("INVALID_VALUE", errField ? errField : "ramp");
    rampLoad(profile);
    return RESP_OK;
  } else if (command.equalsIgnoreCase("rampstartval")) {
    char *endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || v < 0) return makeError("INVALID_VALUE", "ramp");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "ramp");
    rampBuilderSetStart(static_cast<uint16_t>(v));
    return RESP_OK;
  } else if (command.equalsIgnoreCase("rampadd")) {
    char *endptr = nullptr;
    long target = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str() || target < 0) return makeError("INVALID_VALUE", "ramp");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr != ',') return makeError("INVALID_VALUE", "ramp");
    const char *durStr = endptr + 1;
    char *durEnd = nullptr;
    long dur = strtol(durStr, &durEnd, 10);
    if (durEnd == durStr || dur < 0) return makeError("INVALID_VALUE", "ramp");
    while (*durEnd && isspace(*durEnd)) ++durEnd;
    if (*durEnd) return makeError("INVALID_VALUE", "ramp");
    if (!rampBuilderAdd(static_cast<uint16_t>(target), static_cast<uint32_t>(dur)))
      return makeError("INVALID_VALUE", "ramp_full");
    return RESP_OK;
  } else
    return makeError("INVALID_CMD", command.c_str());
}

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

  if (command.equalsIgnoreCase("freq")) {
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "freq");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "freq");

    int freq = static_cast<int>(v);

    if (!changeFreq(freq)) return makeError("INVALID_VALUE", "freq");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("duty")) {
    char* endptr = nullptr;
    double d = strtod(value.c_str(), &endptr);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "duty");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "duty");

    float duty = static_cast<float>(d);

    if (!changeDuty(duty)) return makeError("INVALID_VALUE", "duty");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("move") || command.equalsIgnoreCase("length_mm")) {
    char* endptr = nullptr;
    double d = strtod(value.c_str(), &endptr);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "move");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "move");

    float len = static_cast<float>(d);

    if (!changeLen(len)) return makeError("INVALID_VALUE", "move");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("pwmdur") || command.equalsIgnoreCase("duration_ms")) {
    char* endptr = nullptr;
    double d = strtod(value.c_str(), &endptr);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "pwmdur");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "pwmdur");

    float dur = static_cast<float>(d);

    if (!changeDur(dur)) return makeError("INVALID_VALUE", "pwmdur");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("pwmpin")) {
    int pin = value.toInt();
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "pwmpin");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "pwmpin");

    pin = static_cast<int>(v);

    if (!changePin(pin)) return makeError("INVALID_VALUE", "pwmpin");
    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("dir")) {
    if (value.equalsIgnoreCase("1") || value.equalsIgnoreCase("cw") || value.equalsIgnoreCase("clockwise")) {
      if (isMotorRunning()) stopMotor();
      setMotorDirection(MotorDirection::Clockwise);
      return RESP_OK;
    }
    else if (value.equalsIgnoreCase("0") || value.equalsIgnoreCase("ccw") || value.equalsIgnoreCase("counterclockwise")) {
      if (isMotorRunning()) stopMotor();
      setMotorDirection(MotorDirection::CounterClockwise);
      return RESP_OK;
    }

    return makeError("INVALID_VALUE", "dir");
  }
  else if (command.equalsIgnoreCase("polepairs")) {
    int polePairs = value.toInt();
    char* endptr = nullptr;
    long v = strtol(value.c_str(), &endptr, 10);
    if (endptr == value.c_str()) return makeError("INVALID_VALUE", "polepairs");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "polepairs");

    polePairs = static_cast<int>(v);

    if (polePairs <= 0) return makeError("INVALID_VALUE", "polepairs");
    setMotorPolePairs(polePairs);
    saveConfig();

    return RESP_OK;
  }
  else if (command.equalsIgnoreCase("addpass")) {
    int first = value.indexOf(',');
    int second = value.indexOf(',', first + 1);
    if (first == -1 || second == -1) return makeError("INVALID_VALUE", "addpass");

    String a = value.substring(0, first);
    String b = value.substring(first + 1, second);
    String c = value.substring(second + 1);

    char* endptr = nullptr;
    double dv1 = strtod(a.c_str(), &endptr);
    if (endptr == a.c_str()) return makeError("INVALID_VALUE", "addpass.rpm");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "addpass.rpm");

    double dv2 = strtod(b.c_str(), &endptr);
    if (endptr == b.c_str()) return makeError("INVALID_VALUE", "addpass.accel");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "addpass.accel");

    double dv3 = strtod(c.c_str(), &endptr);
    if (endptr == c.c_str()) return makeError("INVALID_VALUE", "addpass.delay");
    while (*endptr && isspace(*endptr)) ++endptr;
    if (*endptr) return makeError("INVALID_VALUE", "addpass.delay");

    float rpm = static_cast<float>(dv1);
    float accel = static_cast<float>(dv2);
    float delay = static_cast<float>(dv3);

    if (!addPass(rpm, accel, delay)) return makeError("INVALID_VALUE", "addpass");
    return RESP_OK;
  }
  else return makeError("INVALID_CMD");
}

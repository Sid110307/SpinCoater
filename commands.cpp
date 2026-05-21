#include "commands.h"
#include "io.h"
#include "motor.h"
#include "globals.h"

static constexpr char RESP_OK[] = "OK";
static constexpr char RESP_ERR_INVALID_CMD[] = "ERR:INVALID_CMD";
static constexpr char RESP_ERR_INVALID_VALUE[] = "ERR:INVALID_VALUE";
static constexpr char RESP_ERR_SAFETY_LOCKED[] = "ERR:SAFETY_LOCKED";

static String printJSON() {
  String json = getJSONString();
  Serial.println(json);

  return json;
}

String parse(String input) {
  if (input.length() == 0) return RESP_ERR_INVALID_CMD;
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("start")) {
      if (!startMotor()) return RESP_ERR_SAFETY_LOCKED;
      return RESP_OK;
    } else if (input.equalsIgnoreCase("stop")) {
      stopMotor();
      return RESP_OK;
    } else if (input.equalsIgnoreCase("status")) {
      return printJSON();
    } else if (input.equalsIgnoreCase("estop")) {
      setEmergencyLatched(true);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("clearfault")) {
      clearEmergencyLatched();
      clearAlarmLatched();
      stopMotor();

      return RESP_OK;
    }
    return RESP_ERR_INVALID_CMD;
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("freq")) {
    int freq = value.toInt();

    if (!changeFreq(freq)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else if (command.equalsIgnoreCase("duty")) {
    float duty = value.toFloat();

    if (!changeDuty(duty)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else if (command.equalsIgnoreCase("move")) {
    float len = value.toFloat();

    if (!changeLen(len)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else if (command.equalsIgnoreCase("pwmdur")) {
    float dur = value.toFloat();

    if (!changeDur(dur)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else if (command.equalsIgnoreCase("pwmpin")) {
    int pin = value.toInt();

    if (!changePin(pin)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else if (command.equalsIgnoreCase("dir")) {
    if (value.equalsIgnoreCase("1")) {
      if (isMotorRunning()) stopMotor();
      setMotorDirection(MotorDirection::Clockwise);
      return RESP_OK;
    } else if (value.equalsIgnoreCase("0")) {
      if (isMotorRunning()) stopMotor();
      setMotorDirection(MotorDirection::CounterClockwise);
      return RESP_OK;
    }

    return RESP_ERR_INVALID_VALUE;
  } else if (command.equalsIgnoreCase("polepairs")) {
    int polePairs = value.toInt();

    if (polePairs <= 0) return RESP_ERR_INVALID_VALUE;
    setMotorPolePairs(polePairs);
    saveConfig();

    return RESP_OK;
  } else if (command.equalsIgnoreCase("addpass")) {
    int first = value.indexOf(',');
    int second = value.indexOf(',', first + 1);
    if (first == -1 || second == -1) return RESP_ERR_INVALID_VALUE;

    float rpm = value.substring(0, first).toFloat();
    float accel = value.substring(first + 1, second).toFloat();
    float delay = value.substring(second + 1).toFloat();

    if (!addPass(rpm, accel, delay)) return RESP_ERR_INVALID_VALUE;
    return RESP_OK;
  } else return RESP_ERR_INVALID_CMD;
}

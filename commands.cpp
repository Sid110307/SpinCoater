#include "commands.h"
#include "io.h"
#include "motor.h"
#include "globals.h"

String parse(String input) {
  if (input.length() == 0) return "ERR: Invalid cmd";
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("start")) {
      startMotor();
      return "OK";
    }
    else if (input.equalsIgnoreCase("stop")) {
      stopMotor();
      return "OK";
    }
    else if (input.equalsIgnoreCase("showparm")) {
      getJSON(Serial);
      Serial.println();

      getJSON(BT);
      BT.println();

      return "OK";
    }
    return "ERR: Invalid cmd";
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("freq")) {
    int freq = value.toInt();

    if (!changeFreq(freq)) return "ERR: Invalid value";
    return "OK";
  }
  else if (command.equalsIgnoreCase("duty")) {
    float duty = value.toFloat();

    if (!changeDuty(duty)) return "ERR: Invalid value";
    return "OK";
  }
  else if (command.equalsIgnoreCase("move")) {
    float len = value.toFloat();

    if (!changeLen(len)) return "ERR: Invalid value";
    return "OK";
  }
  else if (command.equalsIgnoreCase("pwmdur")) {
    float dur = value.toFloat();

    if (!changeDur(dur)) return "ERR: Invalid value";
    return "OK";
  }
  else if (command.equalsIgnoreCase("pwmpin")) {
    int pin = value.toInt();

    if (!changePin(pin)) return "ERR: Invalid value";
    return "OK";
  }
  else if (command.equalsIgnoreCase("addpass")) {
    int first = value.indexOf(',');
    int second = value.indexOf(',', first + 1);
    if (first == -1 || second == -1) return "ERR: Invalid value";

    float rpm = value.substring(0, first).toFloat();
    float accel = value.substring(first + 1, second).toFloat();
    float delay = value.substring(second + 1).toFloat();

    if (!addPass(rpm, accel, delay)) return "ERR: Invalid value";
    return "OK";
  }
  else {
    return "ERR: Invalid cmd";
  }
}

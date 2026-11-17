#include "auth.h"
#include "commands.h"
#include "safety.h"
#include "hardware.h"
#include "io.h"
#include "modbus_task.h"
#include "motor.h"
#include "motor_profile.h"
#include "ota.h"
#include "ramp.h"

#include <ArduinoJson.h>
#include <cerrno>
#include <cctype>
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
  MotorProfileSummary capture;
  MotorProfileSample samples[PROFILE_EXPORT_BATCH_SAMPLES];
  size_t sampleCount = motorProfileRead(capture, samples, PROFILE_EXPORT_BATCH_SAMPLES);
  PulseSpeedSnapshot speed = getPulseSpeedSnapshot();
  JsonDocument d;
  d["type"] = "profile";

  JsonObject result = d["capture"].to<JsonObject>();
  result["active"] = capture.active;
  result["session"] = capture.sessionId;
  result["elapsed_ms"] = capture.elapsedMs;
  result["sample_count"] = capture.totalSamples;
  result["dropped_samples"] = capture.droppedSamples;
  result["target_rpm"] = capture.targetRpm;
  result["commanded_accel_ms"] = capture.commandedAccelerationMs;
  result["start_rpm"] = capture.startRpm;
  result["current_rpm"] = capture.currentRpm;
  result["peak_rpm"] = capture.peakRpm;
  result["peak_accel_rpm_s"] = capture.peakAccelerationRpmPerSec;
  result["peak_decel_rpm_s"] = capture.peakDecelerationRpmPerSec;
  result["avg_accel_rpm_s"] = capture.averageAccelerationRpmPerSec;
  result["reached_10pct"] = capture.reached10Percent;
  result["reached_90pct"] = capture.reached90Percent;
  result["time_to_10_ms"] = capture.timeTo10PercentMs;
  result["time_to_90_ms"] = capture.timeTo90PercentMs;
  result["rise_10_90_ms"] = capture.riseTime10To90Ms;
  result["sample_period_ms"] = PROFILE_SAMPLE_PERIOD_MS;
  result["rpm_average_periods"] = PROFILE_SPEED_AVERAGE_PERIODS;

  JsonObject pulse = d["speed_pulse"].to<JsonObject>();
  pulse["rpm"] = speed.valid ? speed.rpm : 0.0f;
  pulse["valid"] = speed.valid;
  pulse["frequency_hz"] = speed.valid ? speed.frequencyHz : 0.0f;
  pulse["count"] = speed.pulseCount;
  pulse["age_ms"] = speed.ageMs;
  pulse["last_us"] = speed.lastPeriodUs;
  pulse["avg_us"] = speed.averagePeriodUs;
  pulse["min_us"] = speed.minPeriodUs;
  pulse["max_us"] = speed.maxPeriodUs;
  pulse["jitter_us"] = speed.jitterUs;
  pulse["pole_pairs"] = config.polePairs;

  JsonArray points = d["samples"].to<JsonArray>();
  for (size_t i = 0; i < sampleCount; ++i) {
    JsonArray point = points.add<JsonArray>();
    point.add(samples[i].elapsedUs);
    point.add(samples[i].rpm);
    point.add(samples[i].accelerationRpmPerSec);
    point.add(samples[i].pulseValid);
    point.add(samples[i].busVoltageV);
    point.add(samples[i].busCurrent10mA);
  }

  String o;
  serializeJson(d, o);
  return o;
}

static String packetHex(const uint8_t *bytes, size_t count) {
  static const char digits[] = "0123456789ABCDEF";
  String text;
  text.reserve(count * 3);
  for (size_t i = 0; i < count; ++i) {
    if (i) text += ' ';
    text += digits[bytes[i] >> 4];
    text += digits[bytes[i] & 0x0f];
  }
  return text;
}

static String driverPacketJSON(const String &hex) {
  uint8_t request[DRIVER_PACKET_REQUEST_SIZE];
  if (!parseDriverPacket(hex.c_str(), request))
    return makeError("INVALID_VALUE", "driver_packet");
  DriverPacketResult result;
  bool completed = mbCallDriverPacket(request, result);
  JsonDocument doc;
  doc["type"] = "driver_packet";
  doc["result"] = driverPacketStatusName(result.status);
  doc["tx"] = packetHex(request, sizeof(request));
  doc["rx"] = packetHex(result.response, result.responseLength);
  doc["elapsed_us"] = result.elapsedUs;
  doc["crc_valid"] = result.crcValid;
  if (completed) doc["sent"] = result.transmitted;
  if (result.status == DriverPacketStatus::Exception)
    doc["exception_code"] = result.response[2];
  if (result.status == DriverPacketStatus::Ok && request[1] == 0x03) {
    JsonArray registers = doc["registers"].to<JsonArray>();
    for (size_t i = 3; i + 2 < result.responseLength; i += 2)
      registers.add((result.response[i] << 8) | result.response[i + 1]);
  }
  String output;
  serializeJson(doc, output);
  return output;
}

static bool parseInt(const String &text, long minimum, long maximum, long &out) {
  const char *start = text.c_str();
  char* end = nullptr;

  errno = 0;
  long value = strtol(start, &end, 10);
  if (end == start || errno == ERANGE) return false;
  while (*end && isspace(static_cast<unsigned char>(*end))) ++end;
  if (*end || value < minimum || value > maximum) return false;

  out = value;
  return true;
}

// Commands that can only make things safer or read public state work without the PIN.
static bool openCommand(const String &input) {
  return input.equalsIgnoreCase("status") || input.equalsIgnoreCase("ping") || input.equalsIgnoreCase("stop") ||
         input.equalsIgnoreCase("estop") || input.equalsIgnoreCase("auth");
}

static String authCommand(const String &pin) {
  switch (authTry(pin)) {
    case 0: return RESP_OK;
    case 2: return String("{\"error\":\"LOCKED_OUT\",\"retry_s\":") + authLockoutRemainingS() + "}";
    default: return makeError("AUTH_FAILED");
  }
}

String parse(String input) {
  // Split before trimming: whitespace in a Wi-Fi password is significant.
  int sepIndex = input.indexOf('=');
  String value;
  if (sepIndex >= 0) {
    value = input.substring(sepIndex + 1);
    input = input.substring(0, sepIndex);
  }
  input.trim();
  if (input.length() == 0) return makeError("INVALID_CMD", "Empty command");
  if (!input.equalsIgnoreCase("wifi")) value.trim();

  if (!openCommand(input) && !authSessionOk()) return makeError("AUTH_REQUIRED");

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("ping")) {
      return String("{\"pong\":") + millis() + "}";
    } else if (input.equalsIgnoreCase("reboot")) {
      if (isMotorRunning() || motorBrakeStopPending() || otaInProgress()) return makeError("COMMAND_FAILED", "reboot");
      xTaskCreate([](void *) { vTaskDelay(pdMS_TO_TICKS(300)); ESP.restart(); }, "reboot", 2048, nullptr, 1, nullptr);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("start")) {
      if (!startMotor()) return makeError("SAFETY_LOCKED");
      return RESP_OK;
    } else if (input.equalsIgnoreCase("stop")) {
      rampStop();
      if (!stopMotor()) return makeError("COMMAND_FAILED", "stop");
      return RESP_OK;
    } else if (input.equalsIgnoreCase("status")) {
      return printJSON();
    } else if (input.equalsIgnoreCase("estop")) {
      rampStop();
      safetyRecordTrip("user_estop");
      setEmergencyLatched(true);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("clearfault")) {
      if (!clearEmergencyLatched()) return makeError("SAFETY_LOCKED");
      if (!mbCall(MbOp::ClearFault) || !stopMotor()) {
        setEmergencyLatched(true);  // the driver did not confirm: stay latched
        return makeError("COMMAND_FAILED", "clearfault");
      }
      mbCall(MbOp::RefreshTelemetry);
      safetyClearTrip();

      return RESP_OK;
    } else if (input.equalsIgnoreCase("profile")) {
      return profileJSON();
    } else if (input.equalsIgnoreCase("ota_check") || input.equalsIgnoreCase("ota_install")) {
      if (isMotorRunning() || rampGetState().active) return makeError("SAFETY_LOCKED");
      if (!otaRequestCheck(input.equalsIgnoreCase("ota_install"))) return makeError("OTA_NOT_CONFIGURED");
      return RESP_OK;
    } else if (input.equalsIgnoreCase("ota_status")) {
      return otaStatusJSON();
    } else if (input.equalsIgnoreCase("wifi_clear")) {
      if (otaInProgress()) return makeError("COMMAND_FAILED", "wifi_clear");
      otaClearWifi();
      return RESP_OK;
    }
    return makeError("INVALID_CMD", input.c_str());
  }

  String command = input;

  if (command.equalsIgnoreCase("auth")) {
    return authCommand(value);
  } else if (command.equalsIgnoreCase("driver_packet")) {
    return driverPacketJSON(value);
  } else if (command.equalsIgnoreCase("setpin")) {
    if (!authSetPin(value)) return makeError("INVALID_VALUE", "setpin");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("max_hold_s")) {
    long v;
    if (!parseInt(value, 0, MAX_HOLD_S_MAX, v)) return makeError("INVALID_VALUE", "max_hold_s");
    config.maxHoldS = static_cast<uint16_t>(v);
    saveConfig();
    return RESP_OK;
  } else if (command.equalsIgnoreCase("link_stop")) {
    if (value != "0" && value != "1") return makeError("INVALID_VALUE", "link_stop");
    config.linkStop = value == "1";
    saveConfig();
    return RESP_OK;
  } else if (command.equalsIgnoreCase("profile")) {
    if (value.equalsIgnoreCase("start")) motorProfileStart();
    else if (value.equalsIgnoreCase("stop")) motorProfileStop();
    else if (value.equalsIgnoreCase("reset")) motorProfileReset();
    else return makeError("INVALID_VALUE", "profile");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("wifi")) {
    if (otaInProgress()) return makeError("COMMAND_FAILED", "wifi");
    int separator = value.indexOf('|');
    if (separator <= 0) return makeError("INVALID_VALUE", "wifi");
    String ssid = value.substring(0, separator);
    String password = value.substring(separator + 1);
    if (!otaSetWifi(ssid, password)) return makeError("INVALID_VALUE", "wifi");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("rpm")) {
    long v;
    if (!parseInt(value, CLOSED_LOOP_RPM_MIN, CLOSED_LOOP_RPM_MAX, v))
      return makeError("INVALID_VALUE", "rpm");

    uint16_t rpm = static_cast<uint16_t>(v);

    if (!changeTargetRpm(rpm))
      return makeError("INVALID_VALUE", "rpm");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("open_loop_raw")) {
    long v;
    if (!parseInt(value, OPEN_LOOP_RAW_MIN, OPEN_LOOP_RAW_MAX, v))
      return makeError("INVALID_VALUE", "open_loop_raw");

    uint16_t raw = static_cast<uint16_t>(v);

    if (!changeOpenLoopCommandRaw(raw))
      return makeError("INVALID_VALUE", "open_loop_raw");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("accel_ms")) {
    long v;
    if (!parseInt(value, ACCEL_MS_MIN, ACCEL_MS_MAX, v))
      return makeError("INVALID_VALUE", "accel_ms");

    uint16_t accelMs = static_cast<uint16_t>(v);

    if (!changeAccelerationMs(accelMs))
      return makeError("INVALID_VALUE", "accel_ms");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("dir")) {
    if (value == "1") {
      if (!changeDirection(MotorDirection::Clockwise))
        return makeError("INVALID_VALUE", "dir");
      return RESP_OK;
    } else if (value == "0") {
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
  } else if (command.equalsIgnoreCase("pole_pairs")) {
    long v;
    if (!parseInt(value, POLE_PAIRS_MIN, POLE_PAIRS_MAX, v))
      return makeError("INVALID_VALUE", "pole_pairs");
    if (!changePolePairs(static_cast<uint8_t>(v)))
      return makeError("INVALID_VALUE", "pole_pairs");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("current_limit")) {
    long v;
    if (!parseInt(value, 0, CURRENT_LIMIT_MAX_A, v))
      return makeError("INVALID_VALUE", "current_limit");
    if (!changeCurrentLimitAmps(static_cast<uint8_t>(v)))
      return makeError("INVALID_VALUE", "current_limit");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("pwm_max")) {
    long v;
    if (!parseInt(value, PWM_MAX_MIN, PWM_MAX_MAX, v))
      return makeError("INVALID_VALUE", "pwm_max");
    if (!changePwmMaxPercent(static_cast<uint8_t>(v)))
      return makeError("INVALID_VALUE", "pwm_max");
    return RESP_OK;
  } else if (command.equalsIgnoreCase("ramp")) {
    const char *errField = nullptr;
    if (!rampStartProfile(value.c_str(), &errField))
      return makeError("INVALID_VALUE", errField ? errField : "ramp");
    return RESP_OK;
  } else
    return makeError("INVALID_CMD", command.c_str());
}

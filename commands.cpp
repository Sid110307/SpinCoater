#include "commands.h"
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
  pulse["pole_pairs"] = SPEED_POLE_PAIRS;

  JsonArray points = d["samples"].to<JsonArray>();
  for (size_t i = 0; i < sampleCount; ++i) {
    JsonArray point = points.add<JsonArray>();
    point.add(samples[i].elapsedUs);
    point.add(samples[i].rpm);
    point.add(samples[i].accelerationRpmPerSec);
    point.add(samples[i].pulseValid);
  }

  String o;
  serializeJson(d, o);
  return o;
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

String parse(String input) {
  if (input.length() == 0) return makeError("INVALID_CMD", "Empty command");
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("start")) {
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
      setEmergencyLatched(true);
      return RESP_OK;
    } else if (input.equalsIgnoreCase("clearfault")) {
      if (!clearEmergencyLatched()) return makeError("SAFETY_LOCKED");
      if (!mbCall(MbOp::ClearFault)) return makeError("COMMAND_FAILED", "clearfault");
      if (!stopMotor()) return makeError("COMMAND_FAILED", "stop");
      mbCall(MbOp::RefreshTelemetry);

      return RESP_OK;
    } else if (input.equalsIgnoreCase("profile")) {
      return profileJSON();
    } else if (input.equalsIgnoreCase("ota_check")) {
      if (!otaRequestCheck()) return makeError("OTA_NOT_CONFIGURED");
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

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("profile")) {
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
  } else if (command.equalsIgnoreCase("ramp")) {
    const char *errField = nullptr;
    if (!rampStartProfile(value.c_str(), &errField))
      return makeError("INVALID_VALUE", errField ? errField : "ramp");
    return RESP_OK;
  } else
    return makeError("INVALID_CMD", command.c_str());
}

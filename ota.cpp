#include "ota.h"

#include <Update.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "hardware.h"
#include "motor.h"
#include "ramp.h"

static constexpr uint32_t OTA_IDLE_TIMEOUT_MS = 20000;
static constexpr size_t OTA_CHUNK_MAX = 2048;

static volatile bool active = false;
static size_t imageSize = 0;
static size_t written = 0;
static uint32_t lastDataMs = 0;
static uint32_t restartAtMs = 0;
static String expectedSha;
static mbedtls_sha256_context sha;
static uint8_t chunk[OTA_CHUNK_MAX];

static String error(const char *code) {
  return String("{\"error\":\"") + code + "\",\"field\":\"ota\"}";
}

static String progressJSON() {
  return String("{\"type\":\"ota\",\"state\":\"receiving\",\"written\":") + written + ",\"size\":" + imageSize + "}";
}

static void reset() {
  if (active) {
    Update.abort();
    mbedtls_sha256_free(&sha);
  }
  active = false;
  imageSize = 0;
  written = 0;
}

bool otaInProgress() {
  return active || restartAtMs != 0;
}

String otaStart(const String &value) {
  if (restartAtMs) return error("OTA_BUSY");
  PulseSpeedSnapshot speed = getPulseSpeedSnapshot();
  if (isMotorRunning() || motorBrakeStopPending() || rampGetState().active || (speed.valid && speed.rpm > SPINNING_RPM))
    return error("SAFETY_LOCKED");

  int comma = value.indexOf(',');
  if (comma <= 0) return error("INVALID_VALUE");
  long size = value.substring(0, comma).toInt();
  String hash = value.substring(comma + 1);
  hash.trim();
  hash.toLowerCase();
  if (size <= 0 || hash.length() != 64) return error("INVALID_VALUE");
  for (size_t i = 0; i < hash.length(); ++i)
    if (!isxdigit(static_cast<unsigned char>(hash[i]))) return error("INVALID_VALUE");

  reset();
  if (!Update.begin(static_cast<size_t>(size), U_FLASH)) return error("OTA_SIZE");
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  imageSize = static_cast<size_t>(size);
  expectedSha = hash;
  lastDataMs = millis();
  active = true;
  return progressJSON();
}

String otaData(const String &value) {
  if (!active) return error("OTA_IDLE");
  int comma = value.indexOf(',');
  if (comma <= 0) return error("INVALID_VALUE");
  size_t offset = static_cast<size_t>(value.substring(0, comma).toInt());
  if (offset != written) return progressJSON();

  size_t length = 0;
  const unsigned char *encoded = reinterpret_cast<const unsigned char *>(value.c_str()) + comma + 1;
  if (mbedtls_base64_decode(chunk, sizeof(chunk), &length, encoded, value.length() - comma - 1) != 0 || length == 0)
    return error("OTA_DATA");
  if (written + length > imageSize) {
    reset();
    return error("OTA_SIZE");
  }
  if (Update.write(chunk, length) != length) {
    reset();
    return error("OTA_WRITE");
  }
  mbedtls_sha256_update(&sha, chunk, length);
  written += length;
  lastDataMs = millis();
  return progressJSON();
}

String otaFinish() {
  if (!active) return error("OTA_IDLE");
  if (written != imageSize) return error("OTA_SIZE");

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  String actual;
  actual.reserve(64);
  for (uint8_t b : digest) {
    actual += HEX_DIGITS[b >> 4];
    actual += HEX_DIGITS[b & 0x0F];
  }
  active = false;
  if (actual != expectedSha) {
    Update.abort();
    return error("OTA_CHECKSUM");
  }
  if (!Update.end(true)) return error("OTA_WRITE");

  restartAtMs = millis() + 800;
  return "{\"type\":\"ota\",\"state\":\"installed\"}";
}

String otaCancel() {
  reset();
  return "OK";
}

String otaStatusJSON() {
  String state = restartAtMs ? "installed" : active ? "receiving" : "idle";
  return String("{\"type\":\"ota\",\"current_version\":\"") + FIRMWARE_VERSION + "\",\"state\":\"" + state +
         "\",\"written\":" + written + ",\"size\":" + imageSize + "}";
}

void otaService(bool connected) {
  if (restartAtMs && static_cast<int32_t>(millis() - restartAtMs) >= 0) ESP.restart();
  if (active && (!connected || millis() - lastDataMs > OTA_IDLE_TIMEOUT_MS)) reset();
}

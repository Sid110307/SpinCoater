#include "ota.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "hardware.h"
#include "ramp.h"
#include "rtos.h"

static constexpr char OTA_MANIFEST_URL[] =
  "https://github.com/Sid110307/SpinCoater/releases/latest/download/manifest.json";
static constexpr uint32_t OTA_PERIOD_MS = 24UL * 60UL * 60UL * 1000UL;  // check only; installs need ota_install
static constexpr uint32_t OTA_BOOT_DELAY_MS = 60UL * 1000UL;
static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000;
static constexpr uint32_t NTP_TIMEOUT_MS = 15000;

extern const uint8_t x509BundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509BundleEnd[] asm("_binary_x509_crt_bundle_end");

struct OtaState {
  String state = "not_configured";
  String availableVersion;
  String error;
  uint8_t progress = 0;
  uint32_t lastCheckMs = 0;
  bool wifiConfigured = false;
  bool wifiConnected = false;
};

static SemaphoreHandle_t otaMutex = nullptr;
static TaskHandle_t otaTaskHandle = nullptr;
static OtaState otaState;
static String wifiSsid;
static String wifiPassword;
static bool installRequested = false;
static volatile bool installing = false;

static void lockOta() {
  if (otaMutex) xSemaphoreTake(otaMutex, portMAX_DELAY);
}

static void unlockOta() {
  if (otaMutex) xSemaphoreGive(otaMutex);
}

static void setState(const char *state, const String &error = String()) {
  lockOta();
  otaState.state = state;
  otaState.error = error;
  unlockOta();
}

static bool versionNewer(const String &candidate, const String &current) {
  const char *a = candidate.c_str();
  const char *b = current.c_str();
  while (*a || *b) {
    char *aEnd = nullptr;
    char *bEnd = nullptr;
    unsigned long aPart = strtoul(a, &aEnd, 10);
    unsigned long bPart = strtoul(b, &bEnd, 10);
    if (aPart != bPart) return aPart > bPart;
    if (*aEnd == '.') ++aEnd;
    if (*bEnd == '.') ++bEnd;
    if (aEnd == a && bEnd == b) return candidate > current;
    a = aEnd;
    b = bEnd;
  }
  return false;
}

static void secureClient(NetworkClientSecure &client) {
  client.setCACertBundle(x509BundleStart, x509BundleEnd - x509BundleStart);
  client.setHandshakeTimeout(15);
}

static bool connectWifi() {
  lockOta();
  String ssid = wifiSsid;
  String password = wifiPassword;
  unlockOta();
  if (ssid.isEmpty()) {
    setState("not_configured");
    return false;
  }

  setState("connecting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    vTaskDelay(pdMS_TO_TICKS(250));
  if (WiFi.status() != WL_CONNECTED) {
    setState("failed", "Wi-Fi connection failed");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  lockOta();
  otaState.wifiConnected = true;
  unlockOta();
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com");
  start = millis();
  time_t now = time(nullptr);
  while (now < 1700000000 && millis() - start < NTP_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
    now = time(nullptr);
  }
  if (now < 1700000000) {
    setState("failed", "Time synchronization failed");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    lockOta();
    otaState.wifiConnected = false;
    unlockOta();
    return false;
  }
  return true;
}

static void disconnectWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  lockOta();
  otaState.wifiConnected = false;
  unlockOta();
}

static bool getManifest(String &version, String &url, String &sha256) {
  NetworkClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  if (!http.begin(client, OTA_MANIFEST_URL)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    setState("failed", String("Manifest HTTP ") + code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();
  if (error) {
    setState("failed", "Invalid update manifest");
    return false;
  }
  version = doc["version"] | "";
  url = doc["url"] | "";
  sha256 = doc["sha256"] | "";
  sha256.toLowerCase();
  if (version.isEmpty() || !url.startsWith("https://github.com/") || sha256.length() != 64) {
    setState("failed", "Incomplete update manifest");
    return false;
  }
  for (size_t i = 0; i < sha256.length(); ++i)
    if (!isxdigit(static_cast<unsigned char>(sha256[i]))) {
      setState("failed", "Invalid update checksum");
      return false;
    }
  return true;
}

static String digestHex(const uint8_t digest[32]) {
  static constexpr char HEX_DIGITS[] = "0123456789abcdef";
  String result;
  result.reserve(64);
  for (uint8_t i = 0; i < 32; ++i) {
    result += HEX_DIGITS[digest[i] >> 4];
    result += HEX_DIGITS[digest[i] & 0x0F];
  }
  return result;
}

static bool installUpdate(const String &url, const String &expectedSha256) {
  NetworkClientSecure client;
  secureClient(client);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(20000);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  int size = http.getSize();
  if (code != HTTP_CODE_OK || size <= 0) {
    setState("failed", String("Firmware HTTP ") + code);
    http.end();
    return false;
  }
  if (!Update.begin(static_cast<size_t>(size), U_FLASH)) {
    setState("failed", Update.errorString());
    http.end();
    return false;
  }

  setState("downloading");
  NetworkClient *stream = http.getStreamPtr();
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  uint8_t buffer[4096];
  size_t written = 0;
  uint32_t idleStart = millis();
  while (written < static_cast<size_t>(size)) {
    size_t available = stream->available();
    if (available == 0) {
      if (!http.connected() || millis() - idleStart > 15000) break;
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    size_t count = stream->readBytes(buffer, min(available, sizeof(buffer)));
    if (count == 0) continue;
    idleStart = millis();
    mbedtls_sha256_update(&sha, buffer, count);
    if (Update.write(buffer, count) != count) break;
    written += count;
    lockOta();
    otaState.progress = static_cast<uint8_t>((written * 100ULL) / size);
    unlockOta();
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  http.end();

  if (written != static_cast<size_t>(size)) {
    Update.abort();
    setState("failed", "Incomplete firmware download");
    return false;
  }
  setState("verifying");
  if (!digestHex(digest).equalsIgnoreCase(expectedSha256)) {
    Update.abort();
    setState("failed", "Firmware checksum mismatch");
    return false;
  }
  if (!Update.end(true)) {
    setState("failed", Update.errorString());
    return false;
  }
  setState("installed");
  return true;
}

static void checkForUpdate(bool install) {
  PulseSpeedSnapshot speed = getPulseSpeedSnapshot();
  if (isMotorRunning() || rampGetState().active || (speed.valid && speed.rpm > 20.0f)) {
    setState("deferred", "Motor must be stopped");
    return;
  }
  lockOta();
  otaState.progress = 0;
  otaState.availableVersion = "";
  otaState.lastCheckMs = millis();
  unlockOta();
  if (!connectWifi()) return;
  setState("checking");

  String version;
  String url;
  String sha256;
  if (!getManifest(version, url, sha256)) {
    disconnectWifi();
    return;
  }
  lockOta();
  otaState.availableVersion = version;
  unlockOta();
  if (!versionNewer(version, FIRMWARE_VERSION)) {
    setState("up_to_date");
    disconnectWifi();
    return;
  }
  setState("update_available");
  if (!install) {  // periodic and ota_check runs only report; the user decides
    disconnectWifi();
    return;
  }
  if (!installUpdate(url, sha256)) {
    disconnectWifi();
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
  ESP.restart();
}

static void otaTaskFn(void *) {
  uint32_t wait = OTA_BOOT_DELAY_MS;
  for (;;) {
    bool manual = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait)) > 0;
    bool install = false;
    lockOta();
    bool configured = otaState.wifiConfigured;
    if (manual) {
      install = installRequested;
      installRequested = false;
    }
    unlockOta();
    if (!manual && (!configured || getRuntimeSnapshot().bluetoothConnected)) {  // never disturb a live session
      wait = 60000;
      continue;
    }
    installing = install;
    checkForUpdate(install);
    installing = false;
    lockOta();
    bool deferred = otaState.state == "deferred";
    unlockOta();
    wait = deferred ? 60000 : OTA_PERIOD_MS;
  }
}

void otaBegin() {
  otaMutex = xSemaphoreCreateMutex();
  Preferences otaPrefs;
  otaPrefs.begin("ota", true);
  wifiSsid = otaPrefs.getString("ssid", "");
  wifiPassword = otaPrefs.getString("password", "");
  otaPrefs.end();
  otaState.wifiConfigured = !wifiSsid.isEmpty();
  otaState.state = otaState.wifiConfigured ? "idle" : "not_configured";
  xTaskCreatePinnedToCore(otaTaskFn, "ota", 10240, nullptr, PRIO_OTA, &otaTaskHandle, 0);
}

bool otaSetWifi(const String &ssid, const String &password) {
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) return false;
  Preferences otaPrefs;
  if (!otaPrefs.begin("ota", false)) return false;
  bool ok = otaPrefs.putString("ssid", ssid) > 0;
  if (ok) otaPrefs.putString("password", password);
  otaPrefs.end();
  if (!ok) return false;

  lockOta();
  wifiSsid = ssid;
  wifiPassword = password;
  otaState.wifiConfigured = true;
  otaState.state = "idle";
  otaState.error = "";
  unlockOta();
  return true;
}

void otaClearWifi() {
  Preferences otaPrefs;
  otaPrefs.begin("ota", false);
  otaPrefs.clear();
  otaPrefs.end();

  lockOta();
  wifiSsid = "";
  wifiPassword = "";
  otaState = {};
  unlockOta();
  disconnectWifi();
}

bool otaRequestCheck(bool install) {
  if (!otaTaskHandle) return false;
  lockOta();
  bool configured = otaState.wifiConfigured;
  unlockOta();

  if (!configured) return false;
  lockOta();
  installRequested = install;
  unlockOta();
  xTaskNotifyGive(otaTaskHandle);
  return true;
}

bool otaInProgress() {
  lockOta();
  bool busy = installing || otaState.state == "connecting" || otaState.state == "checking" ||
    otaState.state == "downloading" || otaState.state == "verifying" || otaState.state == "installed";
  unlockOta();

  return busy;
}

String otaStatusJSON() {
  lockOta();
  OtaState snapshot = otaState;
  unlockOta();
  JsonDocument doc;
  doc["type"] = "ota";
  doc["current_version"] = FIRMWARE_VERSION;
  doc["available_version"] = snapshot.availableVersion;
  doc["state"] = snapshot.state;
  doc["progress"] = snapshot.progress;
  doc["error"] = snapshot.error;
  doc["wifi_configured"] = snapshot.wifiConfigured;
  doc["wifi_connected"] = snapshot.wifiConnected;
  doc["last_check_ms"] = snapshot.lastCheckMs;
  String output;
  serializeJson(doc, output);
  return output;
}

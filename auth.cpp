#include "auth.h"

#include <Preferences.h>

static constexpr char DEFAULT_PIN[] = "1234";
static constexpr uint8_t MAX_FAILURES = 5;
static constexpr uint32_t LOCKOUT_MS = 30000;

static String pin;
static bool pinIsDefault = true;
static volatile bool session = false;
static uint8_t failures = 0;
static uint32_t lockedUntil = 0;

static bool constantTimeEquals(const String &a, const String &b) {
  uint8_t diff = a.length() != b.length();
  size_t n = max(a.length(), b.length());
  for (size_t i = 0; i < n; ++i) {
    char x = i < a.length() ? a[i] : 0, y = i < b.length() ? b[i] : 0;
    diff |= static_cast<uint8_t>(x ^ y);
  }
  return diff == 0;
}

static bool validPin(const String &p) {
  if (p.length() < 4 || p.length() > 8) return false;
  for (size_t i = 0; i < p.length(); ++i)
    if (p[i] < '0' || p[i] > '9') return false;
  return true;
}

void authBegin() {
  Preferences p;
  p.begin("auth", true);
  pin = p.getString("pin", "");
  p.end();
  pinIsDefault = !validPin(pin);
  if (pinIsDefault) pin = DEFAULT_PIN;
}

bool authSessionOk() {
  return session;
}

void authReset() {
  session = false;
}

uint32_t authLockoutRemainingS() {
  int32_t d = static_cast<int32_t>(lockedUntil - millis());
  return (failures >= MAX_FAILURES && d > 0) ? (d + 999) / 1000 : 0;
}

int authTry(const String &candidate) {
  if (authLockoutRemainingS() > 0) return 2;
  if (failures >= MAX_FAILURES) failures = 0;  // lock-out expired
  if (constantTimeEquals(candidate, pin)) {
    failures = 0;
    session = true;
    return 0;
  }
  if (++failures >= MAX_FAILURES) lockedUntil = millis() + LOCKOUT_MS;
  return 1;
}

bool authSetPin(const String &p) {
  if (!validPin(p)) return false;
  Preferences prefsAuth;
  if (!prefsAuth.begin("auth", false)) return false;
  bool ok = prefsAuth.putString("pin", p) > 0;
  prefsAuth.end();
  if (!ok) return false;
  pin = p;
  pinIsDefault = false;
  return true;
}

bool authPinIsDefault() {
  return pinIsDefault;
}

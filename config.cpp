#include "config.h"

static bool isValidModbusAddress(uint8_t address) {
  return address >= 1 && address <= 247;
}

static bool isValidControlMode(ControlMode controlMode) {
  return controlMode == ControlMode::OpenLoop || controlMode == ControlMode::ClosedLoop;
}

static bool isValidMotorDirection(MotorDirection direction) {
  return direction == MotorDirection::Clockwise || direction == MotorDirection::CounterClockwise;
}

void loadConfig() {
  bool useDefaults = false;
  prefs.begin("motor", true);
  if (prefs.isKey("cfg")) {
    size_t read = prefs.getBytes("cfg", &config, sizeof(config));
    useDefaults = read != sizeof(config) || config.version != CONFIG_VERSION ||
      !isValidModbusAddress(config.modbusAddress) ||
      !isValidControlMode(config.controlMode) ||
      !isValidMotorDirection(config.direction) ||
      config.targetRpm < CLOSED_LOOP_RPM_MIN || config.targetRpm > CLOSED_LOOP_RPM_MAX ||
      config.openLoopCommandRaw > OPEN_LOOP_RAW_MAX ||
      config.accelerationMs < ACCEL_MS_MIN || config.accelerationMs > ACCEL_MS_MAX;
  } else
    useDefaults = true;
  prefs.end();
  if (useDefaults) {
    setDefaultConfig();
    saveConfig();
  }
}

void saveConfig() {
  prefs.begin("motor", false);
  prefs.putBytes("cfg", &config, sizeof(config));
  prefs.end();
}

void setDefaultConfig() {
  config.version = CONFIG_VERSION;
  config.modbusAddress = DEFAULT_MODBUS_ADDRESS;
  config.controlMode = DEFAULT_CONTROL_MODE;
  config.direction = DEFAULT_DIRECTION;
  config.targetRpm = DEFAULT_TARGET_RPM;
  config.openLoopCommandRaw = DEFAULT_OPEN_LOOP_COMMAND_RAW;
  config.accelerationMs = DEFAULT_ACCELERATION_MS;
}

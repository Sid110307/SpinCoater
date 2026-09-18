/**********************************************************************
 *   Spin Coater Motor Controller
 *   Ver: 0.212 | Date: 18/9/26
 *   Authors: K. Bhaskar, Siddharth P Bharadwaj
 */

#include <Arduino.h>

#include "config.h"
#include "hardware.h"
#include "modbus.h"
#include "ota.h"
#include "rtos.h"

Preferences prefs;
MotorConfig config;

void setup() {
  Serial.begin(115200);

  loadConfig();
  setupHardware();
  setMotorDirection(config.direction);

  if (!modbusBegin(config.modbusAddress)) {
    Serial.println("Modbus init failed");
    setEmergencyLatched(true);
    while (true) delay(1000);
  }

  if (!bluetoothBegin()) {
    Serial.println("BT begin failed");
    setBluetoothConnected(false);
  }

  rtosBegin();
  otaBegin();

  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::LIMIT_SWITCH), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::SPEED), handleSpeedPulseInterrupt, RISING);

  Serial.println("Ready");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

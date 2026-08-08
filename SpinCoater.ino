/**********************************************************************
 *   Spin Coater Motor Controller
 *   Ver: 0.200 | Date: 8/8/26
 *   Authors: K. Bhaskar, Siddharth P Bharadwaj
 */

#include <Arduino.h>

#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "modbus.h"
#include "rtos.h"

BluetoothSerial BT;
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

  if (!BT.begin("SpinCoat")) {
    Serial.println("BT begin failed");
    setBluetoothConnected(false);
  } else {
    BT.setPin("123456", 6);
    BT.println("Ready");
  }

  rtosBegin();

  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::LIMIT_SWITCH), handleEmergencyInterrupt, FALLING);

  Serial.println("Ready");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

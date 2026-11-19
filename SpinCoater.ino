/**********************************************************************
 *   Spin Coater Motor Controller
 *   Ver: 0.305 | Date: 19/11/26
 *   Authors: K. Bhaskar, Siddharth P Bharadwaj
 */

#include <Arduino.h>

#include "auth.h"
#include "config.h"
#include "hardware.h"
#include "modbus.h"
#include "modbus_task.h"
#include "rtos.h"
#include "safety.h"

Preferences prefs;
MotorConfig config;

bool verifyRollbackLater() { return true; }

void setup() {
  Serial.begin(115200);

  loadConfig();
  authBegin();
  setupHardware();
  setMotorDirection(config.direction);

  if (!modbusBegin(config.modbusAddress)) {
    Serial.println("Modbus init failed");
    setEmergencyLatched(true);
    delay(15000);
    ESP.restart();
  }

  if (!bluetoothBegin()) {
    Serial.println("BT begin failed");
    setBluetoothConnected(false);
  }

  rtosBegin();
  safetyBegin();
  mbCallUrgent(MbOp::Run, 0);
  mbCall(MbOp::RefreshOvercurrentAlarm);

  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::LIMIT_SWITCH), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::SPEED), handleSpeedPulseInterrupt, RISING);

  Serial.println("Ready");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

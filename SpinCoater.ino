/**********************************************************************
*   Spin Coater Motor Controller
*   Ver: 0.101 | Date: 29/7/26
*   Authors: K. Bhaskar, Siddharth P Bharadwaj
*/

#include <Arduino.h>

#include "globals.h"
#include "config.h"
#include "motor.h"
#include "modbus.h"
#include "io.h"
#include "commands.h"
#include "hardware.h"

BluetoothSerial BT;
Preferences prefs;
MotorConfig config;

volatile bool sampleDue = false;
hw_timer_t* timer = NULL;

static constexpr size_t COMMAND_BUFFER_SIZE = 128;
char bluetoothCommandBuffer[COMMAND_BUFFER_SIZE];
size_t bluetoothCommandLength = 0;

void IRAM_ATTR onTimer() {
  sampleDue = true;
}

void setup() {
  loadConfig();
  setupHardware();
  Serial.begin(115200);
  setMotorDirection(config.direction);

  if (!modbusBegin(config.modbusAddress)) {
    Serial.println("Modbus init failed");
    setEmergencyLatched(true);

    while (true);
  }

  // Use timer frequency 1MHz, count up
  timer = timerBegin(TIMER_FREQUENCY);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, TIMER_INTERVAL_US, true, 0);

  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::SPEED), handleSpeedInterrupt, RISING);

  if (!BT.begin("SpinCoat")) {
    Serial.println("BT begin failed");
    setBluetoothConnected(false);
  }
  else {
    BT.setPin("123456", 6);
    BT.println("Ready");
  }

  Serial.println("Ready");
}

void loop() {
  setBluetoothConnected(BT.hasClient());
  serviceHardware();

  static bool btOverflow = false;
  while (BT.available() > 0) {
    char incoming = static_cast<char>(BT.read());

    if (incoming == '\r') continue;
    if (incoming == '\n') {
      bluetoothCommandBuffer[bluetoothCommandLength] = '\0';
      if (btOverflow) {
        BT.println("{\"error\":\"BUFFER_OVERFLOW\"}");
      }
      else {
        String response = parse(String(bluetoothCommandBuffer));
        BT.println(response);
      }
      bluetoothCommandLength = 0;
      btOverflow = false;

      continue;
    }

    if (bluetoothCommandLength < COMMAND_BUFFER_SIZE - 1) {
      bluetoothCommandBuffer[bluetoothCommandLength++] = incoming;
    }
    else btOverflow = true;
  }

  if (sampleDue) {
    sampleSpeedRpm();
    sampleDue = false;
  }

  delay(1);
}

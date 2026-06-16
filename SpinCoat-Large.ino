/**********************************************************************
*   Spin Coater Motor Controller
*   Ver: 0.97  Date: 16/6/26
*   Authors: K. Bhaskar, Siddharth P Bharadwaj
*/

#include <Arduino.h>

#include "globals.h"
#include "config.h"
#include "motor.h"
#include "io.h"
#include "commands.h"
#include "hardware.h"

BluetoothSerial BT;
Preferences prefs;
MotorConfig config;

volatile bool sendTelemetry = false;
hw_timer_t* timer = NULL;

static constexpr size_t COMMAND_BUFFER_SIZE = 128;
char bluetoothCommandBuffer[COMMAND_BUFFER_SIZE];
size_t bluetoothCommandLength = 0;

void IRAM_ATTR onTimer() {
  sendTelemetry = true;
}

void setup() {
  loadConfig();
  setupHardware();
  Serial.begin(115200);

  if (!setupPWM()) {
    Serial.println("MCPWM init failed");
    setAlarmLatched(true);
  }

  // Use timer frequency 1MHz, count up
  timer = timerBegin(TIMER_FREQUENCY);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, TIMER_INTERVAL_US, true, 0);

  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::ALM), handleAlarmInterrupt, FALLING);
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

  if (sendTelemetry) {
    sampleSpeedRpm();
    getPasses(BT);  // reference to Stream object of BT
    BT.println();

    sendTelemetry = false;
  }

  delay(1);
}

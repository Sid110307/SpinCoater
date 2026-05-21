/**********************************************************************
*   Motor Controller with Speed and Acceleration at Hardware Interrupt
*   Ver: 0.95  Date: 21/5/26
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
  setupPWM();

  // Use timer 0, frequency 1MHz, count up
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, TIMER_INTERVAL_US, true);
  timerAlarmEnable(timer);

  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(Pins::EMERGENCY), handleEmergencyInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::ALM), handleAlarmInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(Pins::SPEED), handleSpeedInterrupt, RISING);

  BT.begin("SpinCoat");
  BT.setPin("123456");
  BT.println("Ready");
  Serial.begin(115200);
  Serial.println("Ready");
}

void loop() {
  setBluetoothConnected(BT.hasClient());
  serviceHardware();

  while (BT.available() > 0) {
    char incoming = static_cast<char>(BT.read());

    if (incoming == '\r') continue;
    if (incoming == '\n') {
      bluetoothCommandBuffer[bluetoothCommandLength] = '\0';
      String response = parse(String(bluetoothCommandBuffer));
      BT.println(response);
      bluetoothCommandLength = 0;

      continue;
    }

    if (bluetoothCommandLength < COMMAND_BUFFER_SIZE - 1)
      bluetoothCommandBuffer[bluetoothCommandLength++] = incoming;
    else bluetoothCommandLength = 0;
  }

  if (sendTelemetry) {
    sampleSpeedRpm();
    getPasses(BT);  // Reference to Stream object of BT
    BT.println();

    sendTelemetry = false;
  }

  delay(1);
}

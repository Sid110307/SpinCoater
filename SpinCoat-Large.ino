/**********************************************************************
*   Motor Controller with Speed and Acceleration at Hardware Interrupt
*   Ver 0.94  Date : 14/5/26
*   Authors: K. Bhaskar, Siddharth P Bharadwaj
*/

/***********************
* Check out all clashes between hardware interrupts and priorities
* Note : All Hardware interrupt priorities need to be checked
*/

#include <Arduino.h>

#include "globals.h"
#include "config.h"
#include "motor.h"
#include "io.h"
#include "commands.h"

BluetoothSerial BT;
Preferences prefs;
MotorConfig config;

volatile bool sendTelemetry = false;
volatile bool emergencyButtonPressed = false;
volatile unsigned long lastInterruptTime = 0;

hw_timer_t* timer = NULL;

void IRAM_ATTR onTimer() {
  sendTelemetry = true;
}

void IRAM_ATTR handleButtonPress() {
  emergencyButtonPressed = true;
}

void setup() {
  loadConfig();
  setupPWM();

  // Use timer 0, frequency 1MHz, count up
  timer = timerBegin(TIMER_FREQUENCY);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, TIMER_INTERVAL_US, true, 0);

  pinMode(EMERGENCY_BUTTON, INPUT_PULLUP);
  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_BUTTON), handleButtonPress, FALLING);

  BT.begin("SpinCoat");
  BT.println("Ready");
  Serial.begin(115200);
  Serial.println("Ready");
}

void loop() {
  if (BT.available() > 0) {
    String incomingMessage = BT.readStringUntil('\n');
    // Trim to remove trailing carriage return (\r)
    incomingMessage.trim();
    String response = parse(incomingMessage);
    BT.println(response);
  }

  // Debounce
  unsigned long interruptTime = millis();
  if ((interruptTime - lastInterruptTime > DEBOUNCE_MS) && emergencyButtonPressed) {
    lastInterruptTime = interruptTime;

    Serial.println("Emergency Button Pressed!");
    emergencyButtonPressed = false;  // Reset flag
    stopMotor();
  }

  if (sendTelemetry) {
    getPasses(BT); // Reference to Stream object of BT
    BT.println();

    sendTelemetry = false;
  }

  delay(20);
}

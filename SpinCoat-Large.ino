/**********************************************************************
*   Motor Controller with Speed and Acceleration at Hardware Interrupt
*   Ver 0.92  Date : 9/5/26
*   Authors: K. Bhaskar, Siddharth P Bharadwaj
*/

/***********************
* Move out all constants to #define
* Check out all clashes between hardware interrupts and priorities
* Note : All Hardware interrupt priorities need to be checked
* Comment it all.
* Reorganize functions and group variables too
*/
#include "driver/mcpwm.h"
#include "BluetoothSerial.h"
#include "Preferences.h"

#define DEFAULT_FREQ 10000
#define DEFAULT_DUTY 40.5
#define DEFAULT_PWM_PIN 13

#define EMERGENCY_BUTTON 14
#define MAX_PASSES 5
#define CONFIG_VERSION 1  // Change if config params update
#define DEBOUNCE_MS 200
#define TIMER_FREQUENCY 1000000    // 1 MHz
#define TIMER_INTERVAL_US 1000000  // 1,000,000 us = 1s
#define MAX_FREQ 40000000

typedef struct {
  float rpm;
  float accel;
  float delay;
} Pass;

typedef struct {
  int version;
  int frequency;
  float dutyCycle;
  float length;
  float duration;
  int pwmPin;
  int numPasses;
  Pass passes[MAX_PASSES];
} MotorConfig;

const int validPins[] = { 13, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33 };
const int numValidPins = sizeof(validPins) / sizeof(validPins[0]);

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

// ** need to change this function. millis in interrupt is not safe. can move debounce part outside**
void IRAM_ATTR handleButtonPress() {
  unsigned long interruptTime = millis();

  // Software Debounce: ignore interrupts if they happen within DEBOUNCE_MS
  if (interruptTime - lastInterruptTime > DEBOUNCE_MS) emergencyButtonPressed = true;
  lastInterruptTime = interruptTime;
}

void saveConfig() {
  prefs.begin("motor", false);
  prefs.putBytes("cfg", &config, sizeof(config));
  prefs.end();
}

void loadConfig() {
  prefs.begin("motor", true);
  if (prefs.isKey("cfg")) {
    prefs.getBytes("cfg", &config, sizeof(config));
    if (config.version != CONFIG_VERSION) {
      prefs.end();
      return;
    }
  } else {
    config.version = CONFIG_VERSION;
    config.frequency = DEFAULT_FREQ;
    config.dutyCycle = DEFAULT_DUTY;
    config.length = 0;
    config.duration = 0;
    config.pwmPin = DEFAULT_PWM_PIN;
    config.numPasses = 0;
  }
  prefs.end();
}

void setupPWM() {
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, config.pwmPin);

  mcpwm_config_t pwm_config;
  pwm_config.frequency = config.frequency;  // Frequency in Hz
  pwm_config.cmpr_a = config.dutyCycle;     // Initial duty cycle
  pwm_config.cmpr_b = 0;                    // Not used for Generator A
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // Active High

  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, config.dutyCycle);
}

void setupTimer() {
  // Use timer 0, frequency 1MHz, count up
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);          // Attach handler
  timerAlarm(timer, TIMER_INTERVAL_US, true, 0);  // Start
}

void setup() {
  loadConfig();
  setupPWM();
  setupTimer();

  pinMode(EMERGENCY_BUTTON, INPUT_PULLUP);
  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(EMERGENCY_BUTTON), handleButtonPress, FALLING);

  BT.begin("SpinCoat");
  Serial.begin(115200);
}

void loop() {
  if (BT.available() > 0) {
    String incomingMessage = BT.readStringUntil('\n');
    // Trim to remove trailing carriage return (\r)
    incomingMessage.trim();
    String response = parse(incomingMessage);
    BT.println(response);

    delay(50);
  }

  if (emergencyButtonPressed) {
    Serial.println("Emergency Button Pressed!");
    emergencyButtonPressed = false;  // Reset flag
    stopMotor();
  }

  if (sendTelemetry) {
    BT.println(getPasses());
    sendTelemetry = false;
  }

  delay(20);
}

void changeFreq(int freq) {
  config.frequency = freq;
  mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, freq);
  saveConfig();
}

void changeDuty(float duty) {
  config.dutyCycle = duty;
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty);
  saveConfig();
}

void changeLen(float len) {
  config.length = len;
  saveConfig();
}

void changeDur(float dur) {
  config.duration = dur;
  saveConfig();
}

void changePin(int pin) {
  config.pwmPin = pin;
  setupPWM();
  saveConfig();
}

void stopMotor() {
  /* handle the Enable Pin and duty cycle as well*/
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0);
}

bool addPass(float rpm, float accel, float delay) {
  if (config.numPasses >= MAX_PASSES) return false;

  config.passes[config.numPasses].rpm = rpm;
  config.passes[config.numPasses].accel = accel;
  config.passes[config.numPasses].delay = delay;
  config.numPasses++;
  saveConfig();

  return true;
}

bool isValidPWM(int pin) {
  if (pin == EMERGENCY_BUTTON) return false;

  for (int i = 0; i < numValidPins; ++i)
    if (validPins[i] == pin) return true;
  return false;
}

String getPasses() {
  String json = "[";
  for (int i = 0; i < config.numPasses; ++i) {
    json += "{";
    json += "\"rpm\":" + String(config.passes[i].rpm) + ",";
    json += "\"accel\":" + String(config.passes[i].accel) + ",";
    json += "\"delay\":" + String(config.passes[i].delay);
    json += "}";
    if (i < config.numPasses - 1) json += ",";
  }
  json += "]";

  return json;
}

String getJSON() {
  String json = "{";
  json += "\"freq\":" + String(config.frequency) + ",";
  json += "\"duty\":" + String(config.dutyCycle) + ",";
  json += "\"move\":" + String(config.length) + ",";
  json += "\"pwmdur\":" + String(config.duration) + ",";
  json += "\"pwmpin\":" + String(config.pwmPin) + ",";
  json += "\"passes\":" + getPasses();
  json += "}";

  return json;
}

/**
showparm
  Print current parameters as JSON over Serial and BT.

freq=<int>
  Set PWM frequency.
  Range: 1..MAX_FREQ
  Example: freq=1000

duty=<float>
  Set PWM duty cycle %.
  Range: 0..100
  Example: duty=45.5

move=<float>
  Move in mm.
  Range: >= 0
  Example: move=25

pwmdur=<float>
  Set PWM duration.
  Range: >= 0
  Example: pwmdur=500

pwmpin=<int>
  Set PWM output pin.
  Must be a valid and available PWM pin.
  Example: pwmpin=5

addpass=<float>,<float>,<float>
  Add a pass (rpm, acceleration, delay (ms)).
  Example: addpass=1200,50,200
*/
String parse(String input) {
  if (input.length() == 0) return "ERR: Empty input";
  int sepIndex = input.indexOf('=');

  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("showparm")) {
      String json = getJSON();
      Serial.println(json);
      BT.println(json);

      return "OK";
    }
    return "ERR: Invalid cmd";
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("freq")) {
    int freq = value.toInt();
    if (freq <= 0 || freq > MAX_FREQ) return "ERR: Invalid value";
    changeFreq(freq);

    return "OK";
  } else if (command.equalsIgnoreCase("duty")) {
    float duty = value.toFloat();
    if (duty < 0 || duty > 100) return "ERR: Invalid value";
    changeDuty(duty);

    return "OK";
  } else if (command.equalsIgnoreCase("move")) {
    float len = value.toFloat();
    if (len < 0) return "ERR: Invalid value";
    changeLen(len);

    return "OK";
  } else if (command.equalsIgnoreCase("pwmdur")) {
    float dur = value.toFloat();
    if (dur < 0) return "ERR: Invalid value";
    changeDur(dur);

    return "OK";
  } else if (command.equalsIgnoreCase("pwmpin")) {
    int pin = value.toInt();
    if (!isValidPWM(pin)) return "ERR: Invalid value";
    changePin(pin);

    return "OK";
  } else if (command.equalsIgnoreCase("addpass")) {
    float rpm, accel, delay;
    if (sscanf(value.c_str(), "%f,%f,%f", &rpm, &accel, &delay) != 3) return "ERR: Invalid value";
    if (!addPass(rpm, accel, delay)) return "ERR: Max passes";

    return "OK";
  } else {
    return "Invalid";
  }
}

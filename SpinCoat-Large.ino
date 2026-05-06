/**********************************************************************
*   Motor Controller with Speed and Acceleration at Hardware Interrupt
*   Ver 0.90  Date : 6/5/26
*   Authors: K. Bhaskar, Siddharth
*   Note : All Hardware intterupt priorities need to be checked 
*/

/***********************
* Move out all constants to #define
* Check out all clashes between hardware interrupts and priorities
* Comment it all.
* Reorganize functions and group variables too
*/
#include "driver/mcpwm.h"
#include "BluetoothSerial.h"

const int validPins[] = { 4, 5, 12, 13, 14, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33 };
const int numValidPins = sizeof(validPins) / sizeof(validPins[0]);

BluetoothSerial BT;

int frequency = 10000;
float dutyCycle = 40.5;
float length = 0;
float duration = 0;
const int pwmPin = 12;
const int buttonPin = 14;  // GPIO 14 (Connect to button and GND)

volatile bool emergencyButtonPressed = false;
unsigned long lastInterruptTime = 0;

w_timer_t *timer = NULL;  // Initialize timer pointer

void IRAM_ATTR onTimer() {
  // output RPM, accel, etc all important dynamic stuff periodically via BT
}

// Interrupt Service Routine (ISR) - must be in RAM for speed
void IRAM_ATTR handleButtonPress() {
  unsigned long interruptTime = millis();
  // Software Debounce: ignore interrupts if they happen within 200ms
  if (interruptTime - lastInterruptTime > 200) {
    emergencyButtonPressed = true;
  }
  lastInterruptTime = interruptTime;
}

bool isValidPWM(int pin) {
  for (int i = 0; i < numValidPins; ++i)
    if (validPins[i] == pin) return true;
  return false;
}

void setupPWM() {
  mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, pwmPin);

  mcpwm_config_t pwm_config;
  pwm_config.frequency = frequency;  // Frequency in Hz
  pwm_config.cmpr_a = dutyCycle;     // Initial duty cycle (0%)
  pwm_config.cmpr_b = 0;             // Not used for Generator A
  pwm_config.counter_mode = MCPWM_UP_COUNTER;
  pwm_config.duty_mode = MCPWM_DUTY_MODE_0;  // Active High

  mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, dutyCycle);
}

void changeFreq(int freq) {
  frequency = freq;
  mcpwm_set_frequency(MCPWM_UNIT_0, MCPWM_TIMER_0, freq);
}

void changeDuty(float duty) {
  dutyCycle = duty;
  mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, dutyCycle);
}

void changeLen(float len) {
  length = len;
}

void changeDur(float dur) {
  duration = dur;
}

void changePin(int pin) {
  pwmPin = pin;
  setupPWM();
}

void stopMotor() {
  /* handle the Enable Pin and duty cycle as well*/
}

/* implement for pass=int, array {rpm,accel,delay} as many as passes
    Make data persistent across power cycle
    */

String parse(String input) {
  if (input.length() == 0) return "Invalid";
  int sepIndex = input.indexOf('=');
  if (sepIndex == -1) {
    if (input.equalsIgnoreCase("showparm")) {
      String json = "{";
      json += "\"freq\":" + String(frequency) + ",";
      json += "\"duty\":" + String(dutyCycle, 2) + ",";
      json += "\"move\":" + String(length, 2) + ",";
      json += "\"pwmdur\":" + String(duration, 2) + ",";
      json += "\"pwmpin\":" + String(pwmPin);
      /* Add rpm ,accel, delay here */
      json += "}";

      Serial.println(json);
      BT.println(json);

      return "Success";
    } else return "Invalid";
  }

  String command = input.substring(0, sepIndex);
  String value = input.substring(sepIndex + 1);

  command.trim();
  value.trim();

  if (command.equalsIgnoreCase("freq")) {
    int freq = value.toInt();
    if (freq <= 0 || freq > 40000000) {
      Serial.println("Invalid frequency");
      return "Invalid";
    }

    changeFreq(freq);
    Serial.print("Frequency set to: ");
    Serial.println(frequency);

    return "Success";
  } else if (command.equalsIgnoreCase("duty")) {
    float duty = value.toFloat();
    if (duty < 0.0f || duty > 100.0f) {
      Serial.println("Invalid duty cycle");
      return "Invalid";
    }

    changeDuty(duty);
    Serial.print("Duty cycle set to: ");
    Serial.println(dutyCycle);

    return "Success";
  } else if (command.equalsIgnoreCase("move")) {
    float len = value.toFloat();
    if (len < 0.0f) {
      Serial.println("Invalid length");
      return "Invalid";
    }

    changeLen(len);
    Serial.print("Length set to: ");
    Serial.println(length);

    return "Success";
  } else if (command.equalsIgnoreCase("pwmdur")) {
    float dur = value.toFloat();
    if (dur < 0.0f) {
      Serial.println("Invalid duration");
      return "Invalid";
    }

    changeDur(dur);
    Serial.print("Duration set to: ");
    Serial.println(duration);

    return "Success";
  } else if (command.equalsIgnoreCase("pwmpin")) {
    int pin = value.toInt();
    if (!isValidPWM(pin)) {
      Serial.println("Invalid PWM pin");
      return "Invalid";
    }

    changePin(pin);
    Serial.print("Pin set to: ");
    Serial.println(pwmPin);

    return "Success";
  } else {
    Serial.print("Invalid cmd: ");
    Serial.println(input);

    return "Invalid";
  }
}

void setup_timer() {
  // Use timer 0, prescaler 80 (80MHz/80 = 1MHz), count up
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);  // Attach handler
  timerAlarmWrite(timer, 1000000, true);        // 1,000,000 us = 1s
  timerAlarmEnable(timer);                      // Start
}

void setup() {
  setupPWM();
  pinMode(buttonPin, INPUT_PULLUP);

  // Trigger on FALLING (HIGH to LOW) when button is pressed
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleButtonPress, FALLING);

  BT.begin("SpinCoat");
  Serial.begin(115200);
}


void loop() {
  if (BT.available() > 0) {
    String incomingMessage = BT.readStringUntil('\n');
    // Trim to remove trailing carriage return (\r) if necessary
    incomingMessage.trim();
    parse(incomingMessage);
    delay(50);
  }
  if (emergencyButtonPressed) {
    Serial.println("Button Pressed!");
    emergencyButtonPressed = false;  // Reset flag
    stopMotor();
  }
  delay(20);
}

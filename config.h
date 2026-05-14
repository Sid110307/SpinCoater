#pragma once

#include <Arduino.h>
#include <Preferences.h>

extern Preferences prefs;

static constexpr int DEFAULT_FREQ = 10000;
static constexpr float DEFAULT_DUTY = 40.5;
static constexpr int DEFAULT_PWM_PIN = 13;
static constexpr int MAX_FREQ = 40000000;

static constexpr int MAX_PASSES = 5;
static constexpr int CONFIG_VERSION = 1;  // Change if config params update

static constexpr int EMERGENCY_BUTTON = 14;
static constexpr int DEBOUNCE_MS = 200;
static constexpr uint32_t TIMER_FREQUENCY = 1000000;    // 1 MHz
static constexpr uint32_t TIMER_INTERVAL_US = 1000000;  // 1,000,000 us = 1s

struct Pass {
    float rpm;
    float accel;
    float delay;
};

struct MotorConfig {
    int version;
    int frequency;
    float dutyCycle;
    float length;
    float duration;
    int pwmPin;
    int numPasses;
    Pass passes[MAX_PASSES];
};

extern MotorConfig config;

void loadConfig();
void saveConfig();
void setDefaultConfig();

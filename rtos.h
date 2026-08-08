#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static constexpr UBaseType_t PRIO_EMERGENCY = 6;
static constexpr UBaseType_t PRIO_MODBUS = 5;
static constexpr UBaseType_t PRIO_COMMS = 4;
static constexpr UBaseType_t PRIO_RAMP = 3;
static constexpr UBaseType_t PRIO_TELEMETRY = 2;
static constexpr UBaseType_t PRIO_LED = 1;

extern SemaphoreHandle_t gStateMutex;
extern TaskHandle_t gEmergencyTaskHandle;

void rtosBegin();

inline void stateLock() {
  if (gStateMutex) xSemaphoreTake(gStateMutex, portMAX_DELAY);
}
inline void stateUnlock() {
  if (gStateMutex) xSemaphoreGive(gStateMutex);
}

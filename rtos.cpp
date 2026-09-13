#include "rtos.h"

#include <BluetoothSerial.h>

#include "commands.h"
#include "config.h"
#include "hardware.h"
#include "modbus_task.h"
#include "motor_profile.h"
#include "ramp.h"

SemaphoreHandle_t gStateMutex = nullptr;
TaskHandle_t gEmergencyTaskHandle = nullptr;
static BluetoothSerial BT;

static void emergencyTaskFn(void *) {
  static uint32_t lastMs = 0;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (safetyBypassEnabled()) continue;

    uint32_t now = millis();
    if (now - lastMs < DEBOUNCE_MS) continue;
    lastMs = now;

    rampStop();
    setEmergencyLatched(true);
  }
}

static void commsTaskFn(void *) {
  static constexpr size_t BUF = 1024;
  char line[BUF];
  size_t len = 0;
  bool overflow = false;

  for (;;) {
    setBluetoothConnected(BT.hasClient());

    while (BT.available() > 0) {
      char c = static_cast<char>(BT.read());
      if (c == '\r') continue;

      if (c == '\n') {
        line[len] = '\0';
        if (overflow) BT.println("{\"error\":\"BUFFER_OVERFLOW\"}");
        else {
          String response = parse(String(line));
          BT.println(response);
        }

        len = 0;
        overflow = false;
        continue;
      }

      if (len < BUF - 1) line[len++] = c;
      else overflow = true;
    }

    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

static void ledTaskFn(void *) {
  for (;;) {
    updateStatusIndicators();
    vTaskDelay(pdMS_TO_TICKS(LED_REFRESH_MS));
  }
}

bool bluetoothBegin() {
  if (!BT.begin("SpinCoat")) return false;
  BT.println("Ready");
  return true;
}

void rtosBegin() {
  gStateMutex = xSemaphoreCreateMutex();

  modbusTaskBegin();
  rampBegin();
  motorProfileBegin();

  xTaskCreatePinnedToCore(emergencyTaskFn, "emergency", 3072, nullptr, PRIO_EMERGENCY, &gEmergencyTaskHandle, 1);
  xTaskCreatePinnedToCore(ledTaskFn, "led", 2560, nullptr, PRIO_LED, nullptr, 1);
  xTaskCreatePinnedToCore(commsTaskFn, "comms", 6144, nullptr, PRIO_COMMS, nullptr, 0);
}

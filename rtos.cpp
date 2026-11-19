#include "rtos.h"

#include <BluetoothSerial.h>
#include <esp_task_wdt.h>
#include <freertos/stream_buffer.h>

#include "auth.h"
#include "commands.h"
#include "config.h"
#include "hardware.h"
#include "modbus_task.h"
#include "motor.h"
#include "motor_profile.h"
#include "ota.h"
#include "ramp.h"
#include "safety.h"

SemaphoreHandle_t gStateMutex = nullptr;
TaskHandle_t gEmergencyTaskHandle = nullptr;
static BluetoothSerial BT;
static StreamBufferHandle_t rxStream = nullptr;
static constexpr size_t RX_STREAM_BYTES = 8192;

static void emergencyTaskFn(void *) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    if (safetyBypassEnabled()) continue;

    rampStop();
    safetyRecordTrip("safety_input");
    setEmergencyLatched(true);
  }
}

static void commsTaskFn(void *) {
  static constexpr size_t BUF = 4096;
  static char line[BUF];
  size_t len = 0;
  bool overflow = false;
  bool wasConnected = false;
  esp_task_wdt_add(nullptr);

  for (;;) {
    esp_task_wdt_reset();
    bool connected = BT.hasClient();
    setBluetoothConnected(connected);
    if (wasConnected && !connected) {
      authReset();
      xStreamBufferReset(rxStream);
      len = 0;
      overflow = false;
    }
    wasConnected = connected;
    otaService(connected);

    uint8_t rx[256];
    size_t count = xStreamBufferReceive(rxStream, rx, sizeof(rx), pdMS_TO_TICKS(2));
    for (size_t i = 0; i < count; ++i) {
      char c = static_cast<char>(rx[i]);
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
  }
}

static void ledTaskFn(void *) {
  esp_task_wdt_add(nullptr);
  for (;;) {
    esp_task_wdt_reset();
    updateStatusIndicators();
    vTaskDelay(pdMS_TO_TICKS(LED_REFRESH_MS));
  }
}

bool bluetoothBegin() {
  rxStream = xStreamBufferCreate(RX_STREAM_BYTES, 1);
  if (!rxStream) return false;
  BT.onData([](const uint8_t *data, size_t size) { xStreamBufferSend(rxStream, data, size, 0); });
  if (!BT.begin("SpinCoat")) return false;
  BT.println("Ready");
  return true;
}

void rtosBegin() {
  gStateMutex = xSemaphoreCreateMutex();

  motorBegin();
  modbusTaskBegin();
  rampBegin();
  motorProfileBegin();

  xTaskCreatePinnedToCore(emergencyTaskFn, "emergency", 3072, nullptr, PRIO_EMERGENCY, &gEmergencyTaskHandle, 1);
  xTaskCreatePinnedToCore(ledTaskFn, "led", 2560, nullptr, PRIO_LED, nullptr, 1);
  xTaskCreatePinnedToCore(commsTaskFn, "comms", 6144, nullptr, PRIO_COMMS, nullptr, 0);
}

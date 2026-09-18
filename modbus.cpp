#include "modbus.h"

#include "config.h"

#ifndef MODBUS_DEBUG
#define MODBUS_DEBUG 0
#endif

static HardwareSerial ModbusSerial(2);
static uint16_t modbusSlaveAddress = 1;
static bool modbusReady = false;

static constexpr uint32_t MODBUS_BAUDRATE = 115200;
static constexpr uint32_t MODBUS_RESPONSE_TIMEOUT_MS = 60;
static constexpr uint32_t MODBUS_FRAME_GAP_US = 2000;
static constexpr uint32_t MODBUS_DE_SETUP_US = 10;
static constexpr uint32_t MODBUS_TX_TAIL_US = 100;

static uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint16_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
        crc >>= 1;
    }
  }
  return crc;
}

static size_t buildFrame(const uint8_t* payload, size_t payloadLength, uint8_t* frame) {
  for (size_t i = 0; i < payloadLength; ++i) frame[i] = payload[i];

  uint16_t crc = crc16(payload, payloadLength);
  frame[payloadLength] = static_cast<uint8_t>(crc & 0xFF);
  frame[payloadLength + 1] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  return payloadLength + 2;
}

static void clearRxBuffer() {
  while (ModbusSerial.available() > 0) ModbusSerial.read();
}

static bool tx(const uint8_t* request, size_t requestLength, uint8_t* response, size_t responseCapacity, size_t& responseLength) {
  if (!modbusReady) return false;

  clearRxBuffer();
  digitalWrite(Pins::RS485_DE_RE, HIGH);
  delayMicroseconds(MODBUS_DE_SETUP_US);

  ModbusSerial.write(request, requestLength);
  ModbusSerial.flush();
  delayMicroseconds(MODBUS_TX_TAIL_US);
  digitalWrite(Pins::RS485_DE_RE, LOW);

  responseLength = 0;
  uint32_t startMs = millis();
  uint32_t lastByteUs = micros();
  bool gotByte = false;

  for (;;) {
    while (ModbusSerial.available() > 0) {
      uint8_t b = static_cast<uint8_t>(ModbusSerial.read());
      if (responseLength < responseCapacity) response[responseLength++] = b;
      lastByteUs = micros();
      gotByte = true;
    }

    if (gotByte && (micros() - lastByteUs) >= MODBUS_FRAME_GAP_US) break;
    if (!gotByte && (millis() - startMs) >= MODBUS_RESPONSE_TIMEOUT_MS) break;
    taskYIELD();
  }

#if MODBUS_DEBUG
  Serial.printf("TX (%d): ", requestLength);
  for (size_t i = 0; i < requestLength; ++i) Serial.printf("%02X ", request[i]);
  Serial.printf("| RX (%d): ", responseLength);
  for (size_t i = 0; i < responseLength; ++i) Serial.printf("%02X ", response[i]);
  Serial.println();
#endif

  if (!gotByte || responseLength < 5) return false;

  uint16_t receivedCrc = static_cast<uint16_t>(response[responseLength - 2]) | (static_cast<uint16_t>(response[responseLength - 1]) << 8);
  uint16_t calculatedCrc = crc16(response, responseLength - 2);

  return receivedCrc == calculatedCrc;
}

static bool writeRegisterInternal(uint16_t reg, uint16_t value) {
  uint8_t payload[6];
  payload[0] = static_cast<uint8_t>(modbusSlaveAddress);
  payload[1] = 0x06;
  payload[2] = static_cast<uint8_t>((reg >> 8) & 0xFF);
  payload[3] = static_cast<uint8_t>(reg & 0xFF);
  payload[4] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[5] = static_cast<uint8_t>(value & 0xFF);

  uint8_t request[8];
  size_t requestLength = buildFrame(payload, sizeof(payload), request);
  uint8_t response[16];
  size_t responseLength = 0;

  if (!tx(request, requestLength, response, sizeof(response), responseLength))
    return false;
  if (responseLength < 8) return false;
  if (response[0] != modbusSlaveAddress || response[1] != 0x06) return false;

  return response[2] == payload[2] && response[3] == payload[3] && response[4] == payload[4] && response[5] == payload[5];
}

static bool readRegistersInternal(uint16_t reg, uint8_t count, uint16_t* out) {
  uint8_t payload[6];
  payload[0] = static_cast<uint8_t>(modbusSlaveAddress);
  payload[1] = 0x03;
  payload[2] = static_cast<uint8_t>((reg >> 8) & 0xFF);
  payload[3] = static_cast<uint8_t>(reg & 0xFF);
  payload[4] = 0x00;
  payload[5] = count;

  uint8_t request[8];
  size_t requestLength = buildFrame(payload, sizeof(payload), request);
  // header(3) + count*2 + crc(2)
  uint8_t response[5 + 2 * 32];
  size_t responseLength = 0;

  if (!tx(request, requestLength, response, sizeof(response), responseLength))
    return false;
  if (response[0] != modbusSlaveAddress) return false;
  // exception
  if (response[1] == (0x03 | 0x80)) return false;
  if (response[1] != 0x03) return false;

  uint8_t byteCount = response[2];
  if (byteCount != count * 2) return false;
  if (responseLength < static_cast<size_t>(3 + byteCount + 2)) return false;

  for (uint8_t i = 0; i < count; ++i)
    out[i] = static_cast<uint16_t>(response[3 + i * 2] << 8) | response[3 + i * 2 + 1];
  return true;
}

bool modbusBegin(uint16_t slaveAddress) {
  modbusSlaveAddress = slaveAddress;
  pinMode(Pins::RS485_DE_RE, OUTPUT);
  digitalWrite(Pins::RS485_DE_RE, LOW);

  ModbusSerial.begin(MODBUS_BAUDRATE, SERIAL_8N1, Pins::RS485_RX, Pins::RS485_TX);
  ModbusSerial.setTimeout(MODBUS_RESPONSE_TIMEOUT_MS);
  modbusReady = true;

  return true;
}

bool modbusUseCommunicationControl() {
  return writeRegisterInternal(0x4037, 1);
}
bool modbusSetControlMode(bool closedLoop) {
  return writeRegisterInternal(0x4005, closedLoop ? 1 : 0);
}
bool modbusSetClosedLoopRpm(uint16_t rpm) {
  return writeRegisterInternal(0x4006, rpm);
}
bool modbusSetOpenLoopSpeedPercent(uint16_t percent) {
  return writeRegisterInternal(0x4007, percent);
}
bool modbusSetAccelTimeMs(uint16_t ms) {
  return writeRegisterInternal(0x4011, ms);
}
bool modbusSetRun(bool running) {
  return writeRegisterInternal(0x4034, running ? 1 : 0);
}
bool modbusSetDirection(bool clockwise) {
  return writeRegisterInternal(0x4035, clockwise ? 0 : 1);
}
bool modbusSetBrake(bool brakeOn) {
  return writeRegisterInternal(0x4036, brakeOn ? 1 : 0);
}
bool modbusClearFault() {
  return writeRegisterInternal(0x4043, 1);
}

bool modbusReadVoltage(uint16_t& value) {
  return readRegistersInternal(0x4038, 1, &value);
}
bool modbusReadCurrent(uint16_t& value) {
  return readRegistersInternal(0x4039, 1, &value);
}
bool modbusReadPower(uint16_t& value) {
  return readRegistersInternal(0x4040, 1, &value);
}
bool modbusReadSpeed(uint16_t& value) {
  return readRegistersInternal(0x4041, 1, &value);
}
bool modbusReadFault(uint16_t& value) {
  return readRegistersInternal(0x4042, 1, &value);
}

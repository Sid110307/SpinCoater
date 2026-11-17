#include "driver_packet.h"

#include <ctype.h>
#include <string.h>

uint16_t driverPacketCrc(const uint8_t *data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xa001 : 0);
  }
  return crc;
}

static int hexDigit(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool driverPacketRequestValid(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE]) {
  if (request[0] == 0 || request[0] > 247) return false;  // no broadcasts
  if (request[1] != 0x03 && request[1] != 0x06) return false;
  if (request[1] == 0x03) {
    uint16_t count = (request[4] << 8) | request[5];
    uint16_t address = (request[2] << 8) | request[3];
    if (count == 0 || count > 125 || static_cast<uint32_t>(address) + count > 65536) return false;
  }
  uint16_t crc = driverPacketCrc(request, 6);
  return request[6] == (crc & 0xff) && request[7] == (crc >> 8);
}

bool parseDriverPacket(const char *hex, uint8_t request[DRIVER_PACKET_REQUEST_SIZE]) {
  if (!hex) return false;
  size_t count = 0;
  int high = -1;
  for (const unsigned char *p = reinterpret_cast<const unsigned char *>(hex); *p; ++p) {
    if (isspace(*p)) continue;
    int digit = hexDigit(*p);
    if (digit < 0) return false;
    if (high < 0) high = digit;
    else {
      if (count == DRIVER_PACKET_REQUEST_SIZE) return false;
      request[count++] = static_cast<uint8_t>((high << 4) | digit);
      high = -1;
    }
  }
  if (high >= 0 || (count != 6 && count != 8)) return false;
  if (count == 6) {
    uint16_t crc = driverPacketCrc(request, count);
    request[6] = crc & 0xff;
    request[7] = crc >> 8;
  }
  return driverPacketRequestValid(request);
}

void classifyDriverPacketResponse(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE],
                                  DriverPacketResult &result, bool overflow) {
  result.crcValid = false;
  if (overflow || result.responseLength > DRIVER_PACKET_RESPONSE_SIZE) {
    result.status = DriverPacketStatus::Overflow;
    return;
  }
  if (result.responseLength == 0) {
    result.status = DriverPacketStatus::Timeout;
    return;
  }
  if (result.responseLength < 5) {
    result.status = DriverPacketStatus::InvalidResponse;
    return;
  }
  const uint8_t *rx = result.response;
  size_t n = result.responseLength;
  uint16_t crc = driverPacketCrc(rx, n - 2);
  result.crcValid = rx[n - 2] == (crc & 0xff) && rx[n - 1] == (crc >> 8);
  if (!result.crcValid) {
    result.status = DriverPacketStatus::CrcError;
    return;
  }
  result.status = DriverPacketStatus::InvalidResponse;
  if (rx[0] != request[0]) return;
  if (rx[1] == (request[1] | 0x80)) {
    if (n == 5) result.status = DriverPacketStatus::Exception;
    return;
  }
  if (rx[1] != request[1]) return;
  if (request[1] == 0x06) {
    if (n == 8 && memcmp(request, rx, 8) == 0) result.status = DriverPacketStatus::Ok;
  } else {
    size_t expectedBytes = 2 * ((request[4] << 8) | request[5]);
    if (rx[2] == expectedBytes && n == expectedBytes + 5) result.status = DriverPacketStatus::Ok;
  }
}

const char *driverPacketStatusName(DriverPacketStatus status) {
  switch (status) {
    case DriverPacketStatus::Ok: return "ok";
    case DriverPacketStatus::Timeout: return "timeout";
    case DriverPacketStatus::CrcError: return "crc_error";
    case DriverPacketStatus::InvalidResponse: return "invalid_response";
    case DriverPacketStatus::Exception: return "exception";
    case DriverPacketStatus::Overflow: return "overflow";
    case DriverPacketStatus::Busy: return "busy";
    case DriverPacketStatus::SafetyLocked: return "safety_locked";
    case DriverPacketStatus::UnsupportedWrite: return "unsupported_write";
    case DriverPacketStatus::QueueTimeout: return "queue_timeout";
  }
  return "invalid_response";
}

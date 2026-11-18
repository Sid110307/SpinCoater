#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr size_t DRIVER_PACKET_REQUEST_SIZE = 8;
static constexpr size_t DRIVER_PACKET_RESPONSE_SIZE = 256;

enum class DriverPacketStatus : uint8_t {
  Ok, Timeout, CrcError, InvalidResponse, Exception, Overflow,
  Busy, SafetyLocked, UnsupportedWrite, QueueTimeout
};

struct DriverPacketResult {
  DriverPacketStatus status = DriverPacketStatus::QueueTimeout;
  uint8_t response[DRIVER_PACKET_RESPONSE_SIZE]{};
  size_t responseLength = 0;
  uint32_t elapsedUs = 0;
  bool crcValid = false;
  bool transmitted = false;
};

uint16_t driverPacketCrc(const uint8_t *data, size_t length);
bool parseDriverPacket(const char *hex, uint8_t request[DRIVER_PACKET_REQUEST_SIZE]);
bool driverPacketRequestValid(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE]);
void classifyDriverPacketResponse(const uint8_t request[DRIVER_PACKET_REQUEST_SIZE],
                                  DriverPacketResult &result, bool overflow);
const char *driverPacketStatusName(DriverPacketStatus status);

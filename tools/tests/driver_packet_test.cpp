#include "driver_packet.h"
#include <assert.h>
#include <initializer_list>
#include <iostream>
#include <string.h>
#include <vector>

static std::vector<uint8_t> frame(std::initializer_list<uint8_t> payload) {
  std::vector<uint8_t> bytes(payload);
  uint16_t crc = driverPacketCrc(bytes.data(), bytes.size());
  bytes.push_back(crc & 0xff); bytes.push_back(crc >> 8);
  return bytes;
}

int main() {
  const uint8_t standard[] = {'1','2','3','4','5','6','7','8','9'};
  assert(driverPacketCrc(standard, sizeof(standard)) == 0x4b37);
  uint8_t request[8]{};
  const uint8_t knownRead[] = {1, 3, 0x40, 0x41, 0, 1, 0xc1, 0xde};
  const uint8_t knownWrite[] = {1, 6, 0x40, 0x37, 0, 1, 0xec, 4};
  assert(parseDriverPacket("01 03 40 41 00 01", request));
  assert(memcmp(request, knownRead, 8) == 0);
  assert(parseDriverPacket("010640370001", request));
  assert(memcmp(request, knownWrite, 8) == 0);
  assert(parseDriverPacket("01\t03 40 41\n00 01 c1 de", request));
  assert(memcmp(request, knownRead, 8) == 0);
  assert(!parseDriverPacket(nullptr, request));
  for (const char *bad : {"", "01 03 40 41 00", "0103404100001", "010340410001100700",
                          "0103404100010710", "000340410001", "f80340410001", "011040410001",
                          "010340410000", "01034041007e", "0103ffff0002", "01 03 40 41 00 01;start"})
    assert(!parseDriverPacket(bad, request));
  assert(parseDriverPacket("01 03 00 00 00 7d", request));
  assert(parseDriverPacket("01 03 ff ff 00 01", request));
  assert(parseDriverPacket("01 03 40 41 00 01", request));
  DriverPacketResult result;
  auto classify = [&](std::vector<uint8_t> bytes, bool overflow = false) {
    result = {};
    assert(bytes.size() <= sizeof(result.response));
    if (!bytes.empty()) memcpy(result.response, bytes.data(), bytes.size());
    result.responseLength = bytes.size();
    classifyDriverPacketResponse(request, result, overflow);
    return result.status;
  };
  assert(classify({}) == DriverPacketStatus::Timeout);
  assert(classify({1,3}) == DriverPacketStatus::InvalidResponse);
  assert(classify(frame({1,3,2,0,200})) == DriverPacketStatus::Ok && result.crcValid);
  assert(classify(frame({2,3,2,0,200})) == DriverPacketStatus::InvalidResponse);
  assert(classify(frame({1,4,2,0,200})) == DriverPacketStatus::InvalidResponse);
  assert(classify(frame({1,3,4,0,200,0,100})) == DriverPacketStatus::InvalidResponse);
  assert(classify(frame({1,0x83,2})) == DriverPacketStatus::Exception && result.crcValid);
  assert(classify(frame({1,0x83,2,0})) == DriverPacketStatus::InvalidResponse);
  auto corrupted = frame({1,3,2,0,200}); corrupted.back() ^= 1;
  assert(classify(corrupted) == DriverPacketStatus::CrcError && !result.crcValid);
  assert(classify(frame({1,3,2,0,200}), true) == DriverPacketStatus::Overflow);
  assert(parseDriverPacket("01 03 00 00 00 7d", request));
  std::vector<uint8_t> maximum{1, 3, 250};
  for (int i = 0; i < 250; ++i) maximum.push_back(static_cast<uint8_t>(i));
  uint16_t maxCrc = driverPacketCrc(maximum.data(), maximum.size());
  maximum.push_back(maxCrc & 0xff); maximum.push_back(maxCrc >> 8);
  assert(classify(maximum) == DriverPacketStatus::Ok && result.responseLength == 255);
  assert(parseDriverPacket("01 06 40 03 00 03", request));
  assert(classify(frame({1,6,0x40,3,0,3})) == DriverPacketStatus::Ok);
  assert(classify(frame({1,6,0x40,3,0,4})) == DriverPacketStatus::InvalidResponse);
  result.responseLength = DRIVER_PACKET_RESPONSE_SIZE + 1;
  classifyDriverPacketResponse(request, result, false);
  assert(result.status == DriverPacketStatus::Overflow);
  for (unsigned n = 0; n < 256; ++n) {
    result = {}; result.responseLength = n;
    for (unsigned i = 0; i < n; ++i) result.response[i] = static_cast<uint8_t>(i * 17 + n);
    classifyDriverPacketResponse(request, result, false);
  }
  std::cout << "Driver packet framing, CRC, bounds and response checks passed.\n";
}

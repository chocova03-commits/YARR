#include "catch.hpp"

#include "logging.h"

#include <unistd.h>
#include <arpa/inet.h>

#include "ItsdaqHandler.h"

namespace {
auto logger = logging::make_log("test_itsdaq_packet_queue");

struct TestSetup {
  std::vector<std::array<char, 1500>> packet_sequence;

  std::vector<RawData> expected_data;

  std::vector<uint16_t> expected_status;

  std::vector<uint16_t> expected_sys_status;
};

} // End anon namespace

std::array<char, 1500> build_data_packet(const std::vector<uint64_t> &data)
{
  std::array<char, 1500> data_buffer;

  // 7 words of header before opcode payload
  uint16_t *buf16 = (uint16_t*)data_buffer.data();

  uint16_t stream = 0x12;
  uint16_t streamConfig = 0x41;

  buf16[7] = stream;
  // buf16[8] = // Ignored
  buf16[9] = streamConfig;

  // 3 words of header in opcode on top of 7 in wrapper
  uint64_t *data64 = (uint64_t*)&data_buffer[10*2];

  auto h64 = [](uint64_t i) {
    return be64toh(i);
  };

  for(auto d: data) {
    *(data64++) = h64(d);
  }

  // Two words of bad data count
  buf16[10 * 2 + data.size() * 4] = 0;
  buf16[10 * 2 + data.size() * 4 + 1] = 0;

  // Add header trailer last
  // Mostly ignored so don't need to fill in everything
  unsigned total_bytes = data.size() * 8;
  // Include header, opcode, count, trailer words
  total_bytes += (7 + 3 + 2 + 1)*2;
  buf16[2] = htons(total_bytes);
  buf16[3] = htons(1);  // Opcode count
  buf16[4] = htons(0xd004); // Opcode

  return data_buffer;
}

std::array<char, 1500> build_status_packet(uint16_t opcode = 0x19)
{
  std::array<char, 1500> data_buffer;

  uint16_t *buf16 = (uint16_t*)data_buffer.data();

  // Start after header
  unsigned offset = 7;

  // Fill in status regs
  buf16[offset++] = htons(0x1234);
  buf16[offset++] = htons(0x2345);
  buf16[offset++] = htons(0x9876);

  // Mostly ignored
  buf16[0] = 0x8765;    // Magic
  buf16[1] = htons(1);  // Sequence
  buf16[2] = htons((offset + 1) * 2);  // Packet count bytes
  buf16[3] = htons(1);  // Opcode count
  buf16[4] = htons(opcode); // opcode
  buf16[5] = htons(1);    // opcode sequence
  buf16[6] = htons((offset-7) * 2);   // opcode byte length

  // Trailer
  buf16[offset++] = 0xffff;

  return data_buffer;
}

TEST_CASE("ProcessPackets", "[itsdaqFW]") {
  const TestSetup info = GENERATE
    (
     // Test basic data packet
     TestSetup{
       {build_data_packet({
             // Timestamp
             0xff80350000000000,
             // HPR from HCC
             0x403ce0f578500790, 0x7fdc000000000000})},
       {RawData(0x12, {htonl(0xe0f57850), htonl(0x07900000)})}
     },
     // Two packets
     TestSetup{
       {build_data_packet({
             // Timestamp
             0xff80350000000000,
             // HPR from HCC
             0x403ce0f578500790, 0x7fdc000000000000,
             // Timestamp
             0xff80360000000000,
             // HPR from HCC
             0x403ce0f578500790, 0x7fdc000000000000,
           })},
       {RawData(0x12, {htonl(0xe0f57850), htonl(0x07900000)}),
        RawData(0x12, {htonl(0xe0f57850), htonl(0x07900000)})}
     },
     // Two packets without TS between
     TestSetup{
       {build_data_packet({
             // Timestamp
             0xff80350000000000,
             // HPR from HCC
             0x403ce0f578500790, 0x7fdc000000000000,
             // HPR from HCC
             0x403ce0f578500790, 0x7fdc000000000000,
           })},
       {RawData(0x12, {htonl(0xe0f57850), htonl(0x07900000)}),
        RawData(0x12, {htonl(0xe0f57850), htonl(0x07900000)})}
     },
     // Test sys status too
     TestSetup{
       {build_status_packet(0xf9)},
       {}, // Data
       {}, // Status
       {0x1234, 0x2345, 0x9876}
     },
     // Just read status (last in array, without a comma)
     TestSetup{
       {build_status_packet()},
       {}, // Data
       {0x1234, 0x2345, 0x9876}
     }
    );

  // Localhost and auto ports so we don't interfere
  ItsdaqHandler aa(0x0100007f, 0, 0);

  logger->debug("Send packets");
  for(auto &packet: info.packet_sequence) {
    const uint16_t *buf16 = (uint16_t*)packet.data();
    size_t byte_len = ntohs(buf16[2]);

    logger->debug("Send packet length {}", byte_len);

    aa.ProcessPacket(packet, byte_len);
  }

  usleep(10000);

  if(!info.expected_status.empty()) {
    logger->debug("Check for status");
    CHECK(aa.LatestStatus() == info.expected_status);
  }
  if(!info.expected_sys_status.empty()) {
    logger->debug("Check for sys status");
    CHECK(aa.LatestSysStatus() == info.expected_sys_status);
  }

  logger->debug("Check for data");
  for(auto &data: info.expected_data) {
    auto output = aa.GetData();
    REQUIRE(output);

    CHECK(output->getAdr() == data.getAdr());
    CHECK(output->getSize() == data.getSize());
    std::vector<uint32_t> out_data(output->getBuf(), output->getBuf() + output->getSize());
    std::vector<uint32_t> exp_data(data.getBuf(), data.getBuf() + data.getSize());

    CHECK(out_data == exp_data);
  }

  auto remaining_output = aa.GetData();
  // Nothing more
  REQUIRE(!remaining_output);
}

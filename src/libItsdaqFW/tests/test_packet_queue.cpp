#include "catch.hpp"

#include "logging.h"

#include <unistd.h>
#include <arpa/inet.h>

#include "ItsdaqHandler.h"

namespace {
auto logger = logging::make_log("test_itsdaq_packet_queue");

struct TestSetup {
  std::vector<std::array<char, 1500>> packet_sequence;

  std::vector<uint16_t> expected_status;
  std::vector<uint16_t> expected_sys_status;
};

} // End anon namespace

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
     // Test sys status too
     TestSetup{
       {build_status_packet(0xf9)},
       {},
       {0x1234, 0x2345, 0x9876}
     },
     // Just read status (last in array, without a comma)
     TestSetup{
       {build_status_packet()},
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

  auto remaining_output = aa.GetData();
  // Nothing more
  REQUIRE(!remaining_output);
}

#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "gutil/testing.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/string_encodings/readable_byte_string.h"

namespace packetlib {

constexpr char kBanner[] =
    "=========================================================================="
    "======\n";
constexpr char kInputHeader[] =
    "-- INPUT -----------------------------------------------------------------"
    "------\n";
constexpr char kOutputHeader[] =
    "-- OUTPUT ----------------------------------------------------------------"
    "------\n";

void RunPacketParseTest(const std::string& name,
                        const std::string& readable_byte_string) {
  std::cout << kBanner << "Parsing test: " << name << "\n" << kBanner;
  std::cout << kInputHeader << absl::StripAsciiWhitespace(readable_byte_string)
            << "\n"
            << kOutputHeader;

  if (auto byte_string =
          pdpi::ReadableByteStringToByteString(readable_byte_string);
      byte_string.ok()) {
    Packet packet = ParsePacket(*byte_string);
    std::cout << packet.DebugString();
  } else {
    std::cout << "TEST BUG, DO NOT"
              << " SUBMIT! ReadableByteStringToByteString failed: "
              << byte_string.status();
  }
  std::cout << std::endl;
}

// Validates the packet, and if it's not valid, attempts to
// UpdateComputedFields.
void RunProtoPacketTest(const std::string& name, Packet packet) {
  std::cout << kBanner << "Proto packet test: " << name << "\n" << kBanner;
  std::cout << kInputHeader << "packet =" << std::endl
            << packet.DebugString() << std::endl
            << kOutputHeader;

  auto valid = ValidatePacket(packet);
  std::cout << "ValidatePacket(packet) = " << valid << std::endl;

  if (!valid.ok()) {
    std::cout << std::endl << "UpdateComputedFields(packet) = ";
    if (auto updated = UpdateComputedFields(packet); updated.ok()) {
      std::cout << (*updated ? "true" : "false") << std::endl;
      if (*updated) {
        std::cout << "packet =" << std::endl
                  << packet.DebugString() << std::endl;
      }
    } else {
      std::cout << updated.status() << std::endl;
    }
  }

  std::cout << std::endl;
}

void main() {
  RunPacketParseTest("IPv4 packet", R"PB(
    # ethernet header
    ethernet_source: 0x112233445566
    ethernet_destination: 0xaabbccddeeff
    ether_type: 0x0800
    # IPv4 header:
    version: 0x4
    ihl: 0x5
    dhcp: 0b011011
    ecn: 0b01
    total_length: 0x6fc6
    identification: 0xa3cd
    flags: 0b000
    fragment_offset: 0b0000000000000
    ttl: 0x10
    protocol: 0x05  # some unsupported protocol
    checksum: 0x1234
    ipv4_source: 0x0a000001
    ipv4_destination: 0x14000003
    # other headers:
    payload: 0x1234
  )PB");

  RunProtoPacketTest("IPv4 without computed fields",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_source: "11:22:33:44:55:66"
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x0800"
                         }
                       }
                       headers {
                         ipv4_header {
                           ihl: "0x5"
                           dscp: "0x1b"
                           ecn: "0x1"
                           identification: "0xa3cd"
                           flags: "0x0"
                           fragment_offset: "0x0000"
                           ttl: "0x10"
                           protocol: "0x05"
                           ipv4_source: "10.0.0.1"
                           ipv4_destination: "20.0.0.3"
                         }
                       }
                       payload: "0xabcd"
                     )PB"));

  RunProtoPacketTest("IPv4 with various invalid fields",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x0800"
                         }
                       }
                       headers {
                         ipv4_header {
                           version: "0x3"
                           ihl: "0x5k"
                           dscp: "0x1b"
                           ecn: "0x1"
                           identification: "0xa3cd"
                           flags: "0x0"
                           fragment_offset: "0x0000"
                           ttl: "0x10"
                           protocol: "0x05"
                           ipv4_source: "ffff:1::"
                           ipv4_destination: "20.0.0.3"
                         }
                       }
                       payload: "0xabcd"
                     )PB"));
}

}  // namespace packetlib

int main() {
  packetlib::main();
  return 0;
}

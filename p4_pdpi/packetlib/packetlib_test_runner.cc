#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "google/protobuf/util/message_differencer.h"
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
constexpr char kRoundtripHeader[] =
    "-- ROUNDTRIP ERRORS ------------------------------------------------------"
    "------\n";

// Parses packet from bytes, and if it succeeds, re-serializes the parsed packet
// to check that the resulting bytes match the original input.
void RunPacketParseTest(const std::string& name,
                        const std::string& readable_byte_string) {
  std::cout << kBanner << "Parsing test: " << name << "\n" << kBanner;
  std::cout << kInputHeader << absl::StripAsciiWhitespace(readable_byte_string)
            << "\n"
            << kOutputHeader;

  // Attempt to parse.
  auto byte_string = pdpi::ReadableByteStringToByteString(readable_byte_string);
  if (!byte_string.ok()) {
    std::cout << "TEST BUG, DO NOT"
              << " SUBMIT! ReadableByteStringToByteString failed: "
              << byte_string.status() << "\n";
    return;
  }
  Packet packet = ParsePacket(*byte_string);
  std::cout << packet.DebugString() << "\n";

  // Check roundtrip if parsing succeeded.
  if (!packet.reasons_invalid().empty()) return;
  auto byte_string_after_roundtrip = SerializePacket(packet);
  if (!byte_string_after_roundtrip.ok()) {
    std::cout << kRoundtripHeader << byte_string_after_roundtrip.status()
              << "\n";
  } else if (*byte_string_after_roundtrip != *byte_string) {
    std::cout << kRoundtripHeader
              << "Original packet does not match packet after parsing and "
              << "reserialization.\nOriginal packet:\n"
              << absl::BytesToHexString(*byte_string)
              << "\nParsed and reserialized packet:\n"
              << absl::BytesToHexString(*byte_string_after_roundtrip) << "\n";
  }
}

// Validates the packet, and if it's not valid, attempts to
// UpdateComputedFields and revalidate.
// Then attempt to serialize the packet, and if this succeeds, parse the
// serialized packet back and verify that it matches the original packet.
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
        // Try validating once more.
        std::cout << "ValidatePacket(packet) = " << ValidatePacket(packet)
                  << std::endl;
      }
    } else {
      std::cout << updated.status() << std::endl;
    }
  }

  // Try serializing (valid or invalid) packet.
  absl::StatusOr<std::string> bytes = SerializePacket(packet);
  std::cout << "Serialize(Packet) = " << bytes.status() << "\n\n";
  if (!bytes.ok()) return;

  // Test if the packet can be parsed back.
  auto reparsed_packet = ParsePacket(*bytes);
  google::protobuf::util::MessageDifferencer differ;
  std::string diff;
  differ.ReportDifferencesToString(&diff);
  if (!differ.Compare(packet, reparsed_packet)) {
    std::cout << kRoundtripHeader
              << "Original packet does not match packet after serialization "
                 "and reparsing:\n"
              << diff << "\n\n";
  }
}

void main() {
  RunPacketParseTest("Ethernet packet (valid)", R"PB(
    # ethernet header
    ethernet_source: 0x112233445566
    ethernet_destination: 0xaabbccddeeff
    ether_type: 0x0001  # This means size(payload) = 1 byte.
    # payload
    payload: 0x01
  )PB");
  RunPacketParseTest("Ethernet packet (invalid)", R"PB(
    # ethernet header
    ethernet_source: 0x112233445566
    ethernet_destination: 0xaabbccddeeff
    ether_type: 0x0001  # This means size(payload) = 1 byte.
    # payload
    pqyload: 0x0102  # 2 bytes, but ether_type says 1 byte.
  )PB");
  RunPacketParseTest("IPv4 packet (invalid)", R"PB(
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
  RunPacketParseTest("IPv4 packet (valid)", R"PB(
    # ethernet header
    ethernet_source: 0x112233445566
    ethernet_destination: 0xaabbccddeeff
    ether_type: 0x0800
    # IPv4 header:
    version: 0x4
    ihl: 0x5
    dhcp: 0b011011
    ecn: 0b01
    total_length: 0x0016
    identification: 0xa3cd
    flags: 0b000
    fragment_offset: 0b0000000000000
    ttl: 0x10
    protocol: 0x05  # some unsupported protocol
    checksum: 0xb2e7
    ipv4_source: 0x0a000001
    ipv4_destination: 0x14000003
    # other headers:
    payload: 0x1234
  )PB");
  RunPacketParseTest("IPv6 packet (invalid)", R"PB(
    # ethernet header
    ethernet_source: 0x554433221100
    ethernet_destination: 0xffeeddccbbaa
    ether_type: 0x86DD
    # IPv6 header:
    version: 0x4
    dhcp: 0b011011
    ecn: 0b01
    flow_label: 0x12345
    payload_length: 0x0000
    next_header: 0x90  # some unassigned protocol
    hop_limit: 0xff
    ipv6_source: 0x00001111222233334444555566667777
    ipv6_destination: 0x88889999aaaabbbbccccddddeeeeffff
    # other headers:
    payload: 0x12
  )PB");
  RunPacketParseTest("IPv6 packet (valid)", R"PB(
    # ethernet header
    ethernet_source: 0x554433221100
    ethernet_destination: 0xffeeddccbbaa
    ether_type: 0x86DD
    # IPv6 header:
    version: 0x6
    dhcp: 0b011011
    ecn: 0b01
    flow_label: 0x12345
    payload_length: 0x0001
    next_header: 0x90  # some unassigned protocol
    hop_limit: 0x03
    ipv6_source: 0x00001111222233334444555566667777
    ipv6_destination: 0x88889999aaaabbbbccccddddeeeeffff
    # other headers:
    payload: 0x12
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

  RunProtoPacketTest("IPv6 without computed fields",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_source: "11:22:33:44:55:66"
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x86dd"
                         }
                       }
                       headers {
                         ipv6_header {
                           dscp: "0x1b"
                           ecn: "0x1"
                           flow_label: "0x12345"
                           next_header: "0x05"
                           hop_limit: "0x10"
                           ipv6_source: "::"
                           ipv6_destination: "f::f"
                         }
                       }
                       payload: "0xabcd"
                     )PB"));

  RunProtoPacketTest("IPv6 with various invalid fields",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_source: "11:22:33:44:55:66"
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x86dd"
                         }
                       }
                       headers {
                         ipv6_header {
                           version: "0x4"
                           dscp: "1b"
                           ecn: "0b01"
                           flow_label: "0x1234"
                           payload_length: "0x0000"
                           next_header: "0x050"
                           hop_limit: "0x1"
                           ipv6_source: "20.0.0.3"
                           ipv6_destination: ":"
                         }
                       }
                       payload: "0xabcd"
                     )PB"));

  RunProtoPacketTest("IPv6 packet with IPv4 ethertype",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_source: "11:22:33:44:55:66"
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x0800"
                         }
                       }
                       headers {
                         ipv6_header {
                           version: "0x6"
                           dscp: "0x1b"
                           ecn: "0x1"
                           flow_label: "0x12345"
                           payload_length: "0x0000"
                           next_header: "0x05"
                           hop_limit: "0x10"
                           ipv6_source: "::"
                           ipv6_destination: "f::f"
                         }
                       }
                     )PB"));

  RunProtoPacketTest("IPv6 packet without IPv6 header",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_source: "11:22:33:44:55:66"
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethertype: "0x86dd"
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

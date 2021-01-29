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
  // Check roundtrip property modulo `reason_unsupported` field.
  reparsed_packet.clear_reason_unsupported();
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
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0001  # This means size(payload) = 1 byte.
    # payload
    payload: 0x01
  )PB");
  RunPacketParseTest("Ethernet packet (invalid)", R"PB(
    # ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0001  # This means size(payload) = 1 byte.
    # payload
    pqyload: 0x0102  # 2 bytes, but ether_type says 1 byte.
  )PB");
  RunPacketParseTest("Ethernet packet (unsupported EtherType)", R"PB(
    # ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0842  # Wake-on-LAN
  )PB");
  RunPacketParseTest("IPv4 packet (invalid)", R"PB(
    # ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
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
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
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
    checksum: 0xe8a5
    ipv4_source: 0x0a000001
    ipv4_destination: 0x14000003
    # other headers:
    payload: 0x1234
  )PB");
  RunPacketParseTest("IPv4 packet (checksum example)", R"PB(
    # Taken from
    # wikipedia.org/wiki/IPv4_header_checksum#Calculating_the_IPv4_header_checksum
    #
    # ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0800
    # IPv4 header and payload
    ipv4_header: 0x 4500 0073 0000 4000 4011 b861 c0a8 0001 c0a8 00c7
    payload: 0x 0035 e97c 005f 279f 1e4b 8180
  )PB");
  RunPacketParseTest("IPv4 packet with options (valid)", R"PB(
    # Ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0800
    # IPv4 header:
    version: 0x4
    ihl: 0x6  # 5 + 1 x 32-bit suffix
    dhcp: 0b011011
    ecn: 0b01
    total_length: 0x001c
    identification: 0xa3cd
    flags: 0b000
    fragment_offset: 0b0000000000000
    ttl: 0x10
    protocol: 0x05  # some unsupported protocol
    checksum: 0xa339
    ipv4_source: 0x0a000001
    ipv4_destination: 0x14000003
    uninterpreted_suffix: 0x11223344
    # Payload
    payload: 0x55667788
  )PB");
  RunPacketParseTest("IPv4 packet with options (too short)", R"PB(
    # Ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0800
    # IPv4 header:
    version: 0x4
    ihl: 0x6  # 5 + 1 x 32-bit suffix
    dhcp: 0b011011
    ecn: 0b01
    total_length: 0x0018
    identification: 0xa3cd
    flags: 0b000
    fragment_offset: 0b0000000000000
    ttl: 0x10
    protocol: 0x05  # some unsupported protocol
    checksum: 0xd6a3
    ipv4_source: 0x0a000001
    ipv4_destination: 0x14000003
    uninterpreted_suffix: 0x11  # Should be 32 bits, but is only 8 bits.
  )PB");
  RunPacketParseTest("IPv6 packet (invalid)", R"PB(
    # ethernet header
    ethernet_destination: 0xffeeddccbbaa
    ethernet_source: 0x554433221100
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
    ethernet_destination: 0xffeeddccbbaa
    ethernet_source: 0x554433221100
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
  RunPacketParseTest("UDP packet (valid)", R"PB(
    # Taken from
    # www.securitynik.com/2015/08/calculating-udp-checksum-with-taste-of.html
    # --------------------------------------------------------------------------
    # Ethernet header
    ethernet_destination: 0xaabbccddeeff
    ethernet_source: 0x112233445566
    ether_type: 0x0800
    # IPv4 header
    version: 0x4
    ihl: 0x5
    dhcp: 0b011011
    ecn: 0b01
    total_length: 0x001e
    identification: 0x0000
    flags: 0b000
    fragment_offset: 0b0000000000000
    ttl: 0x10
    protocol: 0x11  # UDP
    checksum: 0x28d5
    ipv4_source: 0xc0a8001f       # 192.168.0.31
    ipv4_destination: 0xc0a8001e  # 192.168.0.30
    # UDP header
    source_port: 0x0014       # 20
    destination_port: 0x000a  # 10
    length: 0x000a            # 10
    checksum: 0x35c5
    # Payload
    payload: 0x4869  # "Hi" in ASCII
  )PB");
  RunProtoPacketTest("UDP header not preceded by other header",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         udp_header {
                           source_port: "0x0014"
                           destination_port: "0x000a"
                           length: "0x000a"
                           checksum: "0x35c5"
                         }
                       }
                       payload: "0x4869"
                     )PB"));
  RunProtoPacketTest("UDP header not preceded by IP header",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
                           ethertype: "0x000a"
                         }
                       }
                       headers {
                         udp_header {
                           source_port: "0x0014"
                           destination_port: "0x000a"
                           length: "0x000a"
                           checksum: "0x35c5"
                         }
                       }
                       payload: "0x4869"
                     )PB"));
  RunPacketParseTest("TCP packet (valid)", R"PB(
    # Taken from
    # www.erg.abdn.ac.uk/users/gorry/course/inet-pages/packet-decode3.html
    # --------------------------------------------------------------------------
    # Ethernet header
    ethernet_destination: 0x 00 e0 f7 26 3f e9
    ethernet_source: 0x 08 00 20 86 35 4b
    ether_type: 0x0800
    # IPv4 header
    version: 0x4
    ihl: 0x5
    dhcp: 0b000000
    ecn: 0b00
    total_length: 0x002c
    identification: 0x08b8
    flags: 0b010
    fragment_offset: 0b0000000000000
    ttl: 0xff
    protocol: 0x06  # TCP
    checksum: 0x9997
    ipv4_source: 0x8b85d96e       # 139.133.217.110
    ipv4_destination: 0x8b85e902  # 139.133.233.2
    # TCP header
    source_port: 0x9005          # 36869
    destination_port: 0x0017     # 23 (TELNET)
    sequence_number: 0x7214f114  # 1913975060
    acknowledgement_number: 0x00000000
    data_offset: 0x6  # 6 x 32 bits = 24 bytes
    reserved: 0b000
    flags: 0b 0 0 0 0 0 0 0 1 0  # SYN
    window_size: 0x2238          # 8760
    checksum: 0xa92c
    urgent_pointer: 0x0000
    options: 0x 0204 05b4
  )PB");

  RunProtoPacketTest("IPv4 without computed fields",
                     gutil::ParseProtoOrDie<Packet>(R"PB(
                       headers {
                         ethernet_header {
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
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
                           ihl: "0x6k"
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
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
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
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
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
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
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
                           ethernet_destination: "aa:bb:cc:dd:ee:ff"
                           ethernet_source: "11:22:33:44:55:66"
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

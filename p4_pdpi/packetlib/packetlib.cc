#include "p4_pdpi/packetlib/packetlib.h"

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "glog/logging.h"
#include "gutil/status.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/netaddr/network_address.h"
#include "p4_pdpi/packetlib/bit_widths.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/string_encodings/bit_string.h"
#include "p4_pdpi/utils/hex_string.h"

namespace packetlib {

using ::netaddr::Ipv4Address;
using ::netaddr::MacAddress;

// ---- Parsing ----------------------------------------------------------------

namespace {

// Parser helper functions.  Assumes that there are enough bits left in data.
std::string ParseMacAddress(pdpi::BitString& data) {
  if (auto mac = data.ConsumeMacAddress(); mac.ok()) {
    return mac->ToString();
  } else {
    LOG(DFATAL) << "Size was already checked, should never fail; "
                << mac.status();
    return "INTERNAL ERROR";
  }
}
std::string ParseIpv4Address(pdpi::BitString& data) {
  if (auto ip = data.ConsumeIpv4Address(); ip.ok()) {
    return ip->ToString();
  } else {
    LOG(DFATAL) << "Size was already checked, should never fail; "
                << ip.status();
    return "INTERNAL ERROR";
  }
}
std::string ParseBits(pdpi::BitString& data, int num_bits) {
  if (auto bits = data.ConsumeHexString(num_bits); bits.ok()) {
    return *bits;
  } else {
    LOG(DFATAL) << "Size was already checked, should never fail; "
                << bits.status();
    return "INTERNAL ERROR";
  }
}

// Parse an ethernet header. Returns the next header to be parsed for supported
// ether types.
absl::optional<Header::HeaderCase> ParseEthernetHeader(pdpi::BitString& data,
                                                       Packet& packet) {
  if (data.size() < kStandardEthernetPacketBitwidth) {
    packet.add_reasons_invalid(absl::StrCat(
        "Packet is too short to parse an Ethernet header next. Only ",
        data.size(), " bits left, need at least ",
        kStandardEthernetPacketBitwidth, "."));
    return absl::nullopt;
  }

  EthernetHeader* header = packet.add_headers()->mutable_ethernet_header();
  header->set_ethernet_source(ParseMacAddress(data));
  header->set_ethernet_destination(ParseMacAddress(data));
  header->set_ethertype(ParseBits(data, kEthernetEthertypeBitwidth));

  if (header->ethertype() == "0x0800") return Header::kIpv4Header;
  return absl::nullopt;
}

// Parse an IPv4 header. Returns the next header to be parsed.
absl::optional<Header::HeaderCase> ParseIpv4Header(pdpi::BitString& data,
                                                   Packet& packet) {
  if (data.size() < kStandardIpv4PacketBitwidth) {
    packet.add_reasons_invalid(absl::StrCat(
        "Packet is too short to parse an IPv4 header next. Only ", data.size(),
        " bits left, need at least ", kStandardIpv4PacketBitwidth, "."));
    return absl::nullopt;
  }

  Ipv4Header* header = packet.add_headers()->mutable_ipv4_header();
  header->set_version(ParseBits(data, kIpVersionBitwidth));
  header->set_ihl(ParseBits(data, kIpIhlBitwidth));
  header->set_dscp(ParseBits(data, kIpDscpBitwidth));
  header->set_ecn(ParseBits(data, kIpEcnBitwidth));
  header->set_total_length(ParseBits(data, kIpTotalLengthBitwidth));
  header->set_identification(ParseBits(data, kIpIdentificationBitwidth));
  header->set_flags(ParseBits(data, kIpFlagsBitwidth));
  header->set_fragment_offset(ParseBits(data, kIpFragmentOffsetBitwidth));
  header->set_ttl(ParseBits(data, kIpTtlBitwidth));
  header->set_protocol(ParseBits(data, kIpProtocolBitwidth));
  header->set_checksum(ParseBits(data, kIpChecksumBitwidth));
  header->set_ipv4_source(ParseIpv4Address(data));
  header->set_ipv4_destination(ParseIpv4Address(data));

  // No L4 protocols supported.
  return absl::nullopt;
}

}  // namespace

Packet ParsePacket(absl::string_view input, Header::HeaderCase first_header) {
  pdpi::BitString data = pdpi::BitString::OfByteString(input);
  Packet packet;

  absl::optional<Header::HeaderCase> next_header = first_header;
  while (true) {
    // Done parsing.
    if (data.size() == 0) break;

    // Cannot parse any further.
    if (!next_header.has_value()) {
      if (auto payload = data.ToHexString(); payload.ok()) {
        packet.set_payload(*payload);
      } else {
        LOG(DFATAL) << "Payload should be non-empty";
      }
      break;
    }

    // Parse next header.
    switch (*next_header) {
      case Header::kEthernetHeader:
        next_header = ParseEthernetHeader(data, packet);
        break;
      case Header::kIpv4Header:
        next_header = ParseIpv4Header(data, packet);
        break;
      case Header::HEADER_NOT_SET:
        LOG(DFATAL) << "Unexpected Header::HEADER_NOT_SET while parsing '"
                    << absl::BytesToHexString(input) << "'";
        packet.set_reason_unsupported(
            "Internal error: unexpected HEADER_NOT_SET");
        return packet;
    }
  }

  for (const auto& invalid_reason : PacketInvalidReasons(packet)) {
    packet.add_reasons_invalid(invalid_reason);
  }

  return packet;
}

// ---- Validation -------------------------------------------------------------

absl::Status ValidatePacket(const Packet& packet, bool check_computed_fields) {
  std::vector<std::string> invalid =
      PacketInvalidReasons(packet, check_computed_fields);
  if (invalid.empty()) return absl::OkStatus();
  return gutil::InvalidArgumentErrorBuilder()
         << "Packet invalid for the following reasons:\n- "
         << absl::StrJoin(invalid, "\n- ");
}

namespace {

void MacAddressInvalidReasons(absl::string_view address,
                              const std::string& field,
                              std::vector<std::string>& output) {
  if (address.empty()) {
    output.push_back(absl::StrCat(field, ": missing"));
    return;
  }
  if (auto parsed_address = MacAddress::OfString(address);
      !parsed_address.ok()) {
    output.push_back(absl::StrCat(
        field, ": invalid format: ", parsed_address.status().message()));
  }
}
void Ipv4AddressInvalidReasons(absl::string_view address,
                               const std::string& field,
                               std::vector<std::string>& output) {
  if (address.empty()) {
    output.push_back(absl::StrCat(field, ": missing"));
    return;
  }
  if (auto parsed_address = Ipv4Address::OfString(address);
      !parsed_address.ok()) {
    output.push_back(absl::StrCat(
        field, ": invalid format: ", parsed_address.status().message()));
  }
}
template <size_t num_bits>
void HexStringInvalidReasons(absl::string_view hex_string,
                             const std::string& field,
                             std::vector<std::string>& output) {
  if (hex_string.empty()) {
    output.push_back(absl::StrCat(field, ": missing"));
    return;
  }
  if (auto parsed = pdpi::HexStringToBitset<num_bits>(hex_string);
      !parsed.ok()) {
    output.push_back(
        absl::StrCat(field, ": invalid format: ", parsed.status().message()));
  }
}

void EthernetHeaderInvalidReasons(const EthernetHeader& header,
                                  const std::string& field_prefix,
                                  std::vector<std::string>& output) {
  MacAddressInvalidReasons(header.ethernet_source(),
                           absl::StrCat(field_prefix, "ethernet_source"),
                           output);
  MacAddressInvalidReasons(header.ethernet_destination(),
                           absl::StrCat(field_prefix, "ethernet_destination"),
                           output);
  HexStringInvalidReasons<kEthernetEthertypeBitwidth>(
      header.ethertype(), absl::StrCat(field_prefix, "ethertype"), output);
}

void Ipv4HeaderInvalidReasons(const Ipv4Header& header,
                              bool check_computed_fields,
                              const std::string& field_prefix,
                              const Packet& packet, int header_index,
                              std::vector<std::string>& output) {
  if (check_computed_fields || header.version().empty())
    HexStringInvalidReasons<kIpVersionBitwidth>(
        header.version(), absl::StrCat(field_prefix, "version"), output);
  HexStringInvalidReasons<kIpIhlBitwidth>(
      header.ihl(), absl::StrCat(field_prefix, "ihl"), output);
  HexStringInvalidReasons<kIpDscpBitwidth>(
      header.dscp(), absl::StrCat(field_prefix, "dscp"), output);
  HexStringInvalidReasons<kIpEcnBitwidth>(
      header.ecn(), absl::StrCat(field_prefix, "ecn"), output);
  if (check_computed_fields || header.total_length().empty())
    HexStringInvalidReasons<kIpTotalLengthBitwidth>(
        header.total_length(), absl::StrCat(field_prefix, "total_length"),
        output);
  HexStringInvalidReasons<kIpIdentificationBitwidth>(
      header.identification(), absl::StrCat(field_prefix, "identification"),
      output);
  HexStringInvalidReasons<kIpFlagsBitwidth>(
      header.flags(), absl::StrCat(field_prefix, "flags"), output);
  HexStringInvalidReasons<kIpFragmentOffsetBitwidth>(
      header.fragment_offset(), absl::StrCat(field_prefix, "fragment_offset"),
      output);
  HexStringInvalidReasons<kIpTtlBitwidth>(
      header.ttl(), absl::StrCat(field_prefix, "ttl"), output);
  HexStringInvalidReasons<kIpProtocolBitwidth>(
      header.protocol(), absl::StrCat(field_prefix, "protocol"), output);
  if (check_computed_fields || header.checksum().empty())
    HexStringInvalidReasons<kIpChecksumBitwidth>(
        header.checksum(), absl::StrCat(field_prefix, "checksum"), output);
  Ipv4AddressInvalidReasons(header.ipv4_source(),
                            absl::StrCat(field_prefix, "ipv4_source"), output);
  Ipv4AddressInvalidReasons(header.ipv4_destination(),
                            absl::StrCat(field_prefix, "ipv4_destination"),
                            output);

  // Check computed fields
  if (check_computed_fields && !header.version().empty() &&
      header.version() != "0x4") {
    output.push_back(absl::StrCat(field_prefix,
                                  "version: Must be 0x4, but was ",
                                  header.version(), " instead."));
  }
  if (check_computed_fields && !header.total_length().empty()) {
    if (auto size = PacketSizeInBits(packet, header_index); !size.ok()) {
      output.push_back(absl::StrCat(
          field_prefix, "total_length: Couldn't compute expected size: ",
          size.status().ToString()));
    } else {
      std::string expected =
          pdpi::BitsetToHexString(std::bitset<kIpTotalLengthBitwidth>(*size));
      if (header.total_length() != expected) {
        output.push_back(absl::StrCat(field_prefix, "total_length: Must be ",
                                      expected, ", but was ",
                                      header.total_length(), " instead."));
      }
    }
  }
  if (check_computed_fields && !header.checksum().empty()) {
    if (auto checksum = Ipv4HeaderChecksum(header); !checksum.ok()) {
      output.push_back(absl::StrCat(
          field_prefix, "checksum: Couldn't compute expected checksum: ",
          checksum.status().ToString()));
    } else {
      std::string expected =
          pdpi::BitsetToHexString(std::bitset<kIpChecksumBitwidth>(*checksum));
      if (header.checksum() != expected) {
        output.push_back(absl::StrCat(field_prefix, "checksum: Must be ",
                                      expected, ", but was ", header.checksum(),
                                      " instead."));
      }
    }
  }
}

}  // namespace

std::vector<std::string> PacketInvalidReasons(const Packet& packet,
                                              bool check_computed_fields) {
  std::vector<std::string> result;
  int index = 0;
  for (const Header& header : packet.headers()) {
    std::string field_prefix = absl::StrCat("headers[", index, "].");
    switch (header.header_case()) {
      case Header::kEthernetHeader:
        EthernetHeaderInvalidReasons(header.ethernet_header(), field_prefix,
                                     result);
        break;
      case Header::kIpv4Header: {
        Ipv4HeaderInvalidReasons(header.ipv4_header(), check_computed_fields,
                                 field_prefix, packet, index, result);
        break;
      }
      case Header::HEADER_NOT_SET:
        result.push_back("Found invalid HEADER_NOT_SET in header.");
        break;
    }
    index += 1;
  }
  return result;
}

// ---- Serialization ----------------------------------------------------------

namespace {

absl::Status SerializeMacAddress(absl::string_view address,
                                 pdpi::BitString& output) {
  ASSIGN_OR_RETURN(MacAddress parsed_address, MacAddress::OfString(address));
  output.AppendBits(parsed_address.ToBitset());
  return absl::OkStatus();
}
absl::Status SerializeIpv4Address(absl::string_view address,
                                  pdpi::BitString& output) {
  ASSIGN_OR_RETURN(Ipv4Address parsed_address, Ipv4Address::OfString(address));
  output.AppendBits(parsed_address.ToBitset());
  return absl::OkStatus();
}
template <size_t num_bits>
absl::Status SerializeBits(absl::string_view hex_string,
                           pdpi::BitString& output) {
  ASSIGN_OR_RETURN(auto bitset, pdpi::HexStringToBitset<num_bits>(hex_string));
  output.AppendBits(bitset);
  return absl::OkStatus();
}

absl::Status SerializeEthernetHeader(const EthernetHeader& header,
                                     pdpi::BitString& output) {
  RETURN_IF_ERROR(SerializeMacAddress(header.ethernet_source(), output));
  RETURN_IF_ERROR(SerializeMacAddress(header.ethernet_destination(), output));
  RETURN_IF_ERROR(
      SerializeBits<kEthernetEthertypeBitwidth>(header.ethertype(), output));
  return absl::OkStatus();
}

absl::Status SerializeIpv4Header(const Ipv4Header& header,
                                 pdpi::BitString& output) {
  RETURN_IF_ERROR(SerializeBits<kIpVersionBitwidth>(header.version(), output));
  RETURN_IF_ERROR(SerializeBits<kIpIhlBitwidth>(header.ihl(), output));
  RETURN_IF_ERROR(SerializeBits<kIpDscpBitwidth>(header.dscp(), output));
  RETURN_IF_ERROR(SerializeBits<kIpEcnBitwidth>(header.ecn(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpTotalLengthBitwidth>(header.total_length(), output));
  RETURN_IF_ERROR(SerializeBits<kIpIdentificationBitwidth>(
      header.identification(), output));
  RETURN_IF_ERROR(SerializeBits<kIpFlagsBitwidth>(header.flags(), output));
  RETURN_IF_ERROR(SerializeBits<kIpFragmentOffsetBitwidth>(
      header.fragment_offset(), output));
  RETURN_IF_ERROR(SerializeBits<kIpTtlBitwidth>(header.ttl(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpProtocolBitwidth>(header.protocol(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpChecksumBitwidth>(header.checksum(), output));
  RETURN_IF_ERROR(SerializeIpv4Address(header.ipv4_source(), output));
  RETURN_IF_ERROR(SerializeIpv4Address(header.ipv4_destination(), output));
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<std::string> SerializePacket(Packet packet) {
  RETURN_IF_ERROR(UpdateComputedFields(packet).status());
  RETURN_IF_ERROR(ValidatePacket(packet));
  return RawSerializePacket(packet);
}

absl::StatusOr<std::string> RawSerializePacket(const Packet& packet) {
  pdpi::BitString result;

  for (const Header& header : packet.headers()) {
    switch (header.header_case()) {
      case Header::kEthernetHeader:
        RETURN_IF_ERROR(
            SerializeEthernetHeader(header.ethernet_header(), result));
        break;
      case Header::kIpv4Header:
        RETURN_IF_ERROR(SerializeIpv4Header(header.ipv4_header(), result));
        break;
      case Header::HEADER_NOT_SET:
        return gutil::InvalidArgumentErrorBuilder()
               << "Found invalid HEADER_NOT_SET in header.";
    }
  }
  if (!packet.payload().empty()) {
    ASSIGN_OR_RETURN(auto payload,
                     pdpi::HexStringToByteString(packet.payload()));
    result.AppendBytes(payload);
  }
  return result.ToByteString();
}

// ---- Computed field logic ---------------------------------------------------

absl::StatusOr<bool> UpdateComputedFields(Packet& packet) {
  bool changes = false;

  int header_index = 0;
  for (Header& header : *packet.mutable_headers()) {
    switch (header.header_case()) {
      case Header::kIpv4Header: {
        Ipv4Header& ipv4_header = *header.mutable_ipv4_header();
        if (ipv4_header.version().empty()) {
          ipv4_header.set_version("0x4");
          changes = true;
        }
        if (ipv4_header.total_length().empty()) {
          ASSIGN_OR_RETURN(int size, PacketSizeInBits(packet, header_index));
          ipv4_header.set_total_length(pdpi::BitsetToHexString(
              std::bitset<kIpTotalLengthBitwidth>(size)));
          changes = true;
        }
        if (ipv4_header.checksum().empty()) {
          ASSIGN_OR_RETURN(uint16_t checksum, Ipv4HeaderChecksum(ipv4_header));
          ipv4_header.set_checksum(pdpi::BitsetToHexString(
              std::bitset<kIpChecksumBitwidth>(checksum)));
          changes = true;
        }
        break;
      }
      case Header::kEthernetHeader:
        // No computed fields.
        break;
      case Header::HEADER_NOT_SET:
        return gutil::InvalidArgumentErrorBuilder()
               << "Invalid packet with HEADER_NOT_SET: "
               << packet.DebugString();
    }
    header_index += 1;
  }

  return changes;
}

absl::StatusOr<int> PacketSizeInBits(const Packet& packet,
                                     int start_header_index) {
  if (start_header_index > packet.headers_size() || start_header_index < 0) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid header index " << start_header_index
           << " for a packet with " << packet.headers_size() << " headers.";
  }

  int size = 0;

  for (const Header& header : packet.headers()) {
    switch (header.header_case()) {
      case Header::kIpv4Header:
        size += kStandardIpv4PacketBitwidth;
        break;
      case Header::kEthernetHeader:
        size += kStandardEthernetPacketBitwidth;
        break;
      case Header::HEADER_NOT_SET:
        return gutil::InvalidArgumentErrorBuilder()
               << "Found invalid HEADER_NOT_SET in header.";
    }
  }

  if (!packet.payload().empty()) {
    // Two bytes for '0x' prefix, and then every hex char is 4 bits.
    size += (packet.payload().size() - 2) * 4;
  }

  return size;
}

absl::StatusOr<uint16_t> Ipv4HeaderChecksum(const Ipv4Header& header) {
  // The checksum field is the 16-bit ones' complement of the ones' complement
  // sum of all 16-bit words in the header. For purposes of computing the
  // checksum, the value of the checksum field is zero.

  // We compute the checksum by setting the checksum to 0, serializing the
  // header, and then going over all 16-bit words.
  Ipv4Header copy = header;
  copy.set_checksum("0x0000");
  pdpi::BitString data;
  RETURN_IF_ERROR(SerializeIpv4Header(copy, data));
  uint16_t checksum = 0;
  while (data.size() >= 16) {
    ASSIGN_OR_RETURN(std::bitset<16> word, data.ConsumeBitset<16>());
    checksum += word.to_ulong();
  }
  return checksum;
}

}  // namespace packetlib

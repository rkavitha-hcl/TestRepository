#include "p4_pdpi/packetlib/packetlib.h"

#include <cstddef>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "glog/logging.h"
#include "gutil/overload.h"
#include "gutil/status.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/ipv6_address.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/netaddr/network_address.h"
#include "p4_pdpi/packetlib/bit_widths.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/string_encodings/bit_string.h"
#include "p4_pdpi/string_encodings/hex_string.h"

namespace packetlib {

using ::netaddr::Ipv4Address;
using ::netaddr::Ipv6Address;
using ::netaddr::MacAddress;

namespace {

// -- Determining the header following a given header  -------------------------

// Indicates that a header should follow the current header, but that that
// header is unsupported by packetlib.
struct UnsupportedNextHeader {
  std::string reason;
};

// Encodes header, if any, that should follow the current header.
using NextHeader = absl::variant<
    // A supported next header, or no next header (encoded as HEADER_NOT_SET) if
    // the previous header was the final one before the payload.
    Header::HeaderCase,
    // An unsupported next header.
    UnsupportedNextHeader>;

absl::StatusOr<NextHeader> GetNextHeader(const EthernetHeader& header) {
  ASSIGN_OR_RETURN(int ethertype, pdpi::HexStringToInt(header.ethertype()),
                   _.SetCode(absl::StatusCode::kInternal).SetPrepend()
                       << "unable to parse ethertype: ");
  // See https://en.wikipedia.org/wiki/EtherType.
  if (ethertype <= 1535) return Header::HEADER_NOT_SET;
  if (ethertype == 0x0800) return Header::kIpv4Header;
  if (ethertype == 0x86dd) return Header::kIpv6Header;
  return UnsupportedNextHeader{
      .reason = absl::StrFormat("ethernet_header.ethertype %s: unsupported",
                                header.ethertype())};
}
absl::StatusOr<NextHeader> GetNextHeader(const Ipv4Header& header) {
  if (header.protocol() == "0x06") return Header::kTcpHeaderPrefix;
  if (header.protocol() == "0x11") return Header::kUdpHeader;
  return UnsupportedNextHeader{
      .reason = absl::StrFormat("ipv4_header.protocol %s: unsupported",
                                header.protocol())};
}
absl::StatusOr<NextHeader> GetNextHeader(const Ipv6Header& header) {
  if (header.next_header() == "0x06") return Header::kTcpHeaderPrefix;
  if (header.next_header() == "0x11") return Header::kUdpHeader;
  return UnsupportedNextHeader{
      .reason = absl::StrFormat("ipv6_header.next_header %s: unsupported",
                                header.next_header())};
}
absl::StatusOr<NextHeader> GetNextHeader(const UdpHeader& header) {
  return Header::HEADER_NOT_SET;
}
absl::StatusOr<NextHeader> GetNextHeader(const TcpHeaderPrefix& header) {
  return UnsupportedNextHeader{.reason =
                                   "TCP only partially supported -- parsing "
                                   "prefix of header containing ports only"};
}
absl::StatusOr<NextHeader> GetNextHeader(const Header& header) {
  switch (header.header_case()) {
    case Header::kEthernetHeader:
      return GetNextHeader(header.ethernet_header());
    case Header::kIpv4Header:
      return GetNextHeader(header.ipv4_header());
    case Header::kIpv6Header:
      return GetNextHeader(header.ipv6_header());
    case Header::kUdpHeader:
      return GetNextHeader(header.udp_header());
    case Header::kTcpHeaderPrefix:
      return GetNextHeader(header.tcp_header_prefix());
    case Header::HEADER_NOT_SET:
      return Header::HEADER_NOT_SET;
  }
  return Header::HEADER_NOT_SET;
}

// ---- Parsing ----------------------------------------------------------------

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
std::string ParseIpv6Address(pdpi::BitString& data) {
  if (auto ip = data.ConsumeIpv6Address(); ip.ok()) {
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

// Parse and return an Ethernet header, or return error if the packet is too
// small.
absl::StatusOr<EthernetHeader> ParseEthernetHeader(pdpi::BitString& data) {
  if (data.size() < kEthernetHeaderBitwidth) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet is too short to parse an Ethernet header next. Only "
           << data.size() << " bits left, need at least "
           << kEthernetHeaderBitwidth << ".";
  }

  EthernetHeader header;
  header.set_ethernet_destination(ParseMacAddress(data));
  header.set_ethernet_source(ParseMacAddress(data));
  header.set_ethertype(ParseBits(data, kEthernetEthertypeBitwidth));
  return header;
}

// Parse and return an IPv4 header, or return error if the packet is too small.
absl::StatusOr<Ipv4Header> ParseIpv4Header(pdpi::BitString& data) {
  if (data.size() < kStandardIpv4HeaderBitwidth) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet is too short to parse an IPv4 header next. Only "
           << data.size() << " bits left, need at least "
           << kStandardIpv4HeaderBitwidth << ".";
  }

  Ipv4Header header;
  header.set_version(ParseBits(data, kIpVersionBitwidth));
  header.set_ihl(ParseBits(data, kIpIhlBitwidth));
  header.set_dscp(ParseBits(data, kIpDscpBitwidth));
  header.set_ecn(ParseBits(data, kIpEcnBitwidth));
  header.set_total_length(ParseBits(data, kIpTotalLengthBitwidth));
  header.set_identification(ParseBits(data, kIpIdentificationBitwidth));
  header.set_flags(ParseBits(data, kIpFlagsBitwidth));
  header.set_fragment_offset(ParseBits(data, kIpFragmentOffsetBitwidth));
  header.set_ttl(ParseBits(data, kIpTtlBitwidth));
  header.set_protocol(ParseBits(data, kIpProtocolBitwidth));
  header.set_checksum(ParseBits(data, kIpChecksumBitwidth));
  header.set_ipv4_source(ParseIpv4Address(data));
  header.set_ipv4_destination(ParseIpv4Address(data));

  // Parse suffix/options.
  absl::StatusOr<int> ihl = pdpi::HexStringToInt(header.ihl());
  if (!ihl.ok()) {
    LOG(DFATAL) << "SHOULD NEVER HAPPEN: IHL badly formatted: " << ihl.status();
    // Don't return error status so parsing is lossless despite error.
    // The packet will be invalid, but this will be caught by validity checking.
  } else if (*ihl > 5) {
    int options_bit_width = 32 * (*ihl - 5);
    // If the packet ends prematurely, we still parse what's there to maintain
    // the property that parsing is lossless. The result is an invalid packet,
    // since the IHL and the options length will be inconsistent, but this will
    // be caught by the validity check.
    if (data.size() < options_bit_width) {
      options_bit_width = data.size();
    }
    header.set_uninterpreted_options(ParseBits(data, options_bit_width));
  }
  return header;
}

// Parse and return an IPv6 header, or return error if the packet is too small.
absl::StatusOr<Ipv6Header> ParseIpv6Header(pdpi::BitString& data) {
  if (data.size() < kIpv6HeaderBitwidth) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet is too short to parse an IPv6 header next. Only "
           << data.size() << " bits left, need at least " << kIpv6HeaderBitwidth
           << ".";
  }

  Ipv6Header header;
  header.set_version(ParseBits(data, kIpVersionBitwidth));
  header.set_dscp(ParseBits(data, kIpDscpBitwidth));
  header.set_ecn(ParseBits(data, kIpEcnBitwidth));
  header.set_flow_label(ParseBits(data, kIpFlowLabelBitwidth));
  header.set_payload_length(ParseBits(data, kIpPayloadLengthBitwidth));
  header.set_next_header(ParseBits(data, kIpNextHeaderBitwidth));
  header.set_hop_limit(ParseBits(data, kIpHopLimitBitwidth));
  header.set_ipv6_source(ParseIpv6Address(data));
  header.set_ipv6_destination(ParseIpv6Address(data));
  return header;
}

// Parse a UDP header, or return error if the packet is too small.
absl::StatusOr<UdpHeader> ParseUdpHeader(pdpi::BitString& data) {
  if (data.size() < kUdpHeaderBitwidth) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet is too short to parse an UDP header next. Only "
           << data.size() << " bits left, need at least " << kUdpHeaderBitwidth
           << ".";
  }

  UdpHeader header;
  header.set_source_port(ParseBits(data, kUdpPortBitwidth));
  header.set_destination_port(ParseBits(data, kUdpPortBitwidth));
  header.set_length(ParseBits(data, kUdpLengthBitwidth));
  header.set_checksum(ParseBits(data, kUdpChecksumBitwidth));
  return header;
}

// Parse a TCP header prefix, or return error if the packet is too small.
absl::StatusOr<TcpHeaderPrefix> ParseTcpHeaderPrefix(pdpi::BitString& data) {
  if (data.size() < kTcpHeaderPrefixBitwidth) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Packet is too short to parse a TCP header next. Only "
           << data.size() << " bits left, need at least "
           << kTcpHeaderPrefixBitwidth << ".";
  }

  TcpHeaderPrefix header;
  header.set_source_port(ParseBits(data, kTcpPortBitwidth));
  header.set_destination_port(ParseBits(data, kTcpPortBitwidth));
  return header;
}

absl::StatusOr<Header> ParseHeader(Header::HeaderCase header_case,
                                   pdpi::BitString& data) {
  Header result;
  switch (header_case) {
    case Header::kEthernetHeader: {
      ASSIGN_OR_RETURN(*result.mutable_ethernet_header(),
                       ParseEthernetHeader(data));
      return result;
    }
    case Header::kIpv4Header: {
      ASSIGN_OR_RETURN(*result.mutable_ipv4_header(), ParseIpv4Header(data));
      return result;
    }
    case Header::kIpv6Header: {
      ASSIGN_OR_RETURN(*result.mutable_ipv6_header(), ParseIpv6Header(data));
      return result;
    }
    case Header::kUdpHeader: {
      ASSIGN_OR_RETURN(*result.mutable_udp_header(), ParseUdpHeader(data));
      return result;
    }
    case Header::kTcpHeaderPrefix: {
      ASSIGN_OR_RETURN(*result.mutable_tcp_header_prefix(),
                       ParseTcpHeaderPrefix(data));
      return result;
    }
    case Header::HEADER_NOT_SET:
      break;
  }
  return gutil::InvalidArgumentErrorBuilder()
         << "unexpected HeaderCase: " << HeaderCaseName(header_case);
}

}  // namespace

Packet ParsePacket(absl::string_view input, Header::HeaderCase first_header) {
  pdpi::BitString data = pdpi::BitString::OfByteString(input);
  Packet packet;

  // Parse headers.
  Header::HeaderCase next_header = first_header;
  while (next_header != Header::HEADER_NOT_SET) {
    absl::StatusOr<Header> header = ParseHeader(next_header, data);
    if (!header.ok()) {
      packet.add_reasons_invalid(std::string(header.status().message()));
      break;
    }
    *packet.add_headers() = *header;
    if (absl::StatusOr<NextHeader> next = GetNextHeader(*header); next.ok()) {
      absl::visit(
          gutil::Overload{[&](Header::HeaderCase next) { next_header = next; },
                          [&](UnsupportedNextHeader unsupported) {
                            next_header = Header::HEADER_NOT_SET;
                            packet.set_reason_unsupported(unsupported.reason);
                          }},
          *next);
    } else {
      LOG(DFATAL) << "SHOULD NEVER HAPPEN: " << next.status();
      next_header = Header::HEADER_NOT_SET;
    }
  }

  // Set payload.
  if (data.size() != 0) {
    auto payload = data.ToHexString();
    if (payload.ok()) {
      packet.set_payload(*payload);
    } else {
      LOG(DFATAL) << payload.status();
    }
  }

  // Check packet validity.
  for (const auto& invalid_reason : PacketInvalidReasons(packet)) {
    packet.add_reasons_invalid(invalid_reason);
  }

  return packet;
}

// ---- Validation -------------------------------------------------------------

absl::Status ValidatePacket(const Packet& packet) {
  std::vector<std::string> invalid = PacketInvalidReasons(packet);
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
void Ipv6AddressInvalidReasons(absl::string_view address,
                               const std::string& field,
                               std::vector<std::string>& output) {
  if (address.empty()) {
    output.push_back(absl::StrCat(field, ": missing"));
    return;
  }
  if (auto parsed_address = Ipv6Address::OfString(address);
      !parsed_address.ok()) {
    output.push_back(absl::StrCat(
        field, ": invalid format: ", parsed_address.status().message()));
  }
}
// Returns `true` if invalid, `false` otherwise.
template <size_t num_bits>
bool HexStringInvalidReasons(absl::string_view hex_string,
                             const std::string& field,
                             std::vector<std::string>& output) {
  if (hex_string.empty()) {
    output.push_back(absl::StrCat(field, ": missing"));
    return true;
  }
  if (auto parsed = pdpi::HexStringToBitset<num_bits>(hex_string);
      !parsed.ok()) {
    output.push_back(
        absl::StrCat(field, ": invalid format: ", parsed.status().message()));
    return true;
  }
  return false;
}

bool Ipv4UninterpretedOptionsInvalidReasons(
    absl::string_view uninterpreted_options, const std::string& error_prefix,
    std::vector<std::string>& output) {
  if (uninterpreted_options.empty()) return false;
  if (auto bytes = pdpi::HexStringToByteString(uninterpreted_options);
      !bytes.ok()) {
    output.push_back(absl::StrCat(
        error_prefix, ": invalid format: ", bytes.status().message()));
    return true;
  } else if (int num_bits = bytes->size() * 8; num_bits % 32 != 0) {
    output.push_back(absl::StrCat(error_prefix, ": found ", num_bits,
                                  " bits, but expected multiple of 32 bits"));
    return true;
  }
  return false;
}

void EthernetHeaderInvalidReasons(const EthernetHeader& header,
                                  const std::string& field_prefix,
                                  const Packet& packet, int header_index,
                                  std::vector<std::string>& output) {
  MacAddressInvalidReasons(header.ethernet_destination(),
                           absl::StrCat(field_prefix, "ethernet_destination"),
                           output);
  MacAddressInvalidReasons(header.ethernet_source(),
                           absl::StrCat(field_prefix, "ethernet_source"),
                           output);
  bool ethertype_invalid = HexStringInvalidReasons<kEthernetEthertypeBitwidth>(
      header.ethertype(), absl::StrCat(field_prefix, "ethertype"), output);

  // Check EtherType, see https://en.wikipedia.org/wiki/EtherType.
  if (!ethertype_invalid) {
    auto ethertype = pdpi::HexStringToInt(header.ethertype());
    if (!ethertype.ok()) {
      LOG(DFATAL) << field_prefix
                  << "ethertype invalid despite previous check: "
                  << ethertype.status();
      output.push_back(absl::StrCat(field_prefix, "ethertype: INTERNAL ERROR: ",
                                    ethertype.status().ToString()));
    } else if (*ethertype <= 1500) {
      // `+1` to skip this (and previous) headers in the calculation.
      if (auto size = PacketSizeInBytes(packet, header_index + 1); !size.ok()) {
        output.push_back(absl::StrCat("packet size could not be computed: ",
                                      size.status().ToString()));
      } else if (*ethertype != *size) {
        output.push_back(
            absl::StrFormat("%sethertype: value %s is <= 1500 and should thus "
                            "match payload size, but payload size is %d bytes",
                            field_prefix, header.ethertype(), *size));
      }
    }
  }
}

void Ipv4HeaderInvalidReasons(const Ipv4Header& header,
                              const std::string& field_prefix,
                              const Packet& packet, int header_index,
                              std::vector<std::string>& output) {
  bool version_invalid = HexStringInvalidReasons<kIpVersionBitwidth>(
      header.version(), absl::StrCat(field_prefix, "version"), output);
  bool ihl_invalid = HexStringInvalidReasons<kIpIhlBitwidth>(
      header.ihl(), absl::StrCat(field_prefix, "ihl"), output);
  HexStringInvalidReasons<kIpDscpBitwidth>(
      header.dscp(), absl::StrCat(field_prefix, "dscp"), output);
  HexStringInvalidReasons<kIpEcnBitwidth>(
      header.ecn(), absl::StrCat(field_prefix, "ecn"), output);
  bool length_invalid = HexStringInvalidReasons<kIpTotalLengthBitwidth>(
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
  bool checksum_invalid = HexStringInvalidReasons<kIpChecksumBitwidth>(
      header.checksum(), absl::StrCat(field_prefix, "checksum"), output);
  Ipv4AddressInvalidReasons(header.ipv4_source(),
                            absl::StrCat(field_prefix, "ipv4_source"), output);
  Ipv4AddressInvalidReasons(header.ipv4_destination(),
                            absl::StrCat(field_prefix, "ipv4_destination"),
                            output);
  bool options_invalid = Ipv4UninterpretedOptionsInvalidReasons(
      header.uninterpreted_options(),
      absl::StrCat(field_prefix, "uninterpreted_options"), output);

  // Check computed fields.
  if (!ihl_invalid) {
    if (options_invalid) {
      output.push_back(absl::StrCat(field_prefix,
                                    "ihl: Correct value undefined since "
                                    "uninterpreted_options is invalid."));
    } else {
      absl::string_view options = header.uninterpreted_options();
      // 4 bits for every hex char after "0x"-prefix.
      int options_bitwidth = options.empty() ? 0 : 4 * (options.size() - 2);
      int num_32bit_words = 5 + options_bitwidth / 32;
      std::string expected =
          pdpi::BitsetToHexString<kIpIhlBitwidth>(num_32bit_words);
      if (header.ihl() != expected) {
        output.push_back(absl::StrCat(field_prefix, "ihl: Must be ", expected,
                                      ", but was ", header.ihl(), " instead."));
      }
    }
  }
  if (!version_invalid && header.version() != "0x4") {
    output.push_back(absl::StrCat(field_prefix,
                                  "version: Must be 0x4, but was ",
                                  header.version(), " instead."));
  }
  if (!length_invalid) {
    if (auto size = PacketSizeInBytes(packet, header_index); !size.ok()) {
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
  if (!checksum_invalid) {
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

void Ipv6HeaderInvalidReasons(const Ipv6Header& header,
                              const std::string& field_prefix,
                              const Packet& packet, int header_index,
                              std::vector<std::string>& output) {
  bool version_invalid = HexStringInvalidReasons<kIpVersionBitwidth>(
      header.version(), absl::StrCat(field_prefix, "version"), output);
  HexStringInvalidReasons<kIpDscpBitwidth>(
      header.dscp(), absl::StrCat(field_prefix, "dscp"), output);
  HexStringInvalidReasons<kIpEcnBitwidth>(
      header.ecn(), absl::StrCat(field_prefix, "ecn"), output);
  HexStringInvalidReasons<kIpFlowLabelBitwidth>(
      header.flow_label(), absl::StrCat(field_prefix, "flow_label"), output);
  bool length_invalid = HexStringInvalidReasons<kIpPayloadLengthBitwidth>(
      header.payload_length(), absl::StrCat(field_prefix, "payload_length"),
      output);
  HexStringInvalidReasons<kIpNextHeaderBitwidth>(
      header.next_header(), absl::StrCat(field_prefix, "next_header"), output);
  HexStringInvalidReasons<kIpHopLimitBitwidth>(
      header.hop_limit(), absl::StrCat(field_prefix, "hop_limit"), output);
  Ipv6AddressInvalidReasons(header.ipv6_source(),
                            absl::StrCat(field_prefix, "ipv6_source"), output);
  Ipv6AddressInvalidReasons(header.ipv6_destination(),
                            absl::StrCat(field_prefix, "ipv6_destination"),
                            output);

  // Check computed fields.
  if (!version_invalid && header.version() != "0x6") {
    output.push_back(absl::StrCat(field_prefix,
                                  "version: Must be 0x6, but was ",
                                  header.version(), " instead."));
  }
  if (!length_invalid) {
    // `+1` to skip the IPv6 header and previous headers in the calculation.
    if (auto size = PacketSizeInBytes(packet, header_index + 1); !size.ok()) {
      output.push_back(absl::StrCat(
          field_prefix, "total_length: Couldn't compute expected size: ",
          size.status().ToString()));
    } else {
      std::string expected =
          pdpi::BitsetToHexString(std::bitset<kIpPayloadLengthBitwidth>(*size));
      if (header.payload_length() != expected) {
        output.push_back(absl::StrCat(field_prefix, "payload_length: Must be ",
                                      expected, ", but was ",
                                      header.payload_length(), " instead."));
      }
    }
  }
}

void UdpHeaderInvalidReasons(const UdpHeader& header,
                             const std::string& field_prefix,
                             const Packet& packet, int header_index,
                             std::vector<std::string>& output) {
  HexStringInvalidReasons<kUdpPortBitwidth>(
      header.source_port(), absl::StrCat(field_prefix, "source_port"), output);
  HexStringInvalidReasons<kUdpPortBitwidth>(
      header.destination_port(), absl::StrCat(field_prefix, "destination_port"),
      output);
  bool length_invalid = HexStringInvalidReasons<kUdpLengthBitwidth>(
      header.length(), absl::StrCat(field_prefix, "length"), output);
  bool checksum_invalid = HexStringInvalidReasons<kUdpChecksumBitwidth>(
      header.checksum(), absl::StrCat(field_prefix, "checksum"), output);

  // Check computed field: length.
  if (!length_invalid) {
    if (auto size = PacketSizeInBytes(packet, header_index); !size.ok()) {
      output.push_back(absl::StrCat(field_prefix,
                                    "length: Couldn't compute expected size: ",
                                    size.status().ToString()));
    } else {
      std::string expected =
          pdpi::BitsetToHexString(std::bitset<kUdpLengthBitwidth>(*size));
      if (header.length() != expected) {
        output.push_back(absl::StrCat(field_prefix, "length: Must be ",
                                      expected, ", but was ", header.length(),
                                      " instead."));
      }
    }
  }
  // Check computed field: checksum.
  if (header_index <= 0) {
    output.push_back(absl::StrCat(
        field_prefix,
        "checksum: UDP header must be preceded by IP header for checksum to be "
        "defined; found no header instead"));
  } else if (auto previous = packet.headers(header_index - 1).header_case();
             previous != Header::kIpv4Header &&
             previous != Header::kIpv6Header) {
    output.push_back(absl::StrCat(
        field_prefix,
        "checksum: UDP header must be preceded by IP header for checksum to be "
        "defined; found ",
        HeaderCaseName(previous), " at headers[", (header_index - 1),
        "] instead"));
  } else if (!checksum_invalid) {
    if (auto checksum = UdpHeaderChecksum(packet, header_index);
        !checksum.ok()) {
      output.push_back(absl::StrCat(
          field_prefix, "checksum: Couldn't compute expected checksum: ",
          checksum.status().ToString()));
    } else {
      std::string expected =
          pdpi::BitsetToHexString(std::bitset<kUdpChecksumBitwidth>(*checksum));
      if (header.checksum() != expected) {
        output.push_back(absl::StrCat(field_prefix, "checksum: Must be ",
                                      expected, ", but was ", header.checksum(),
                                      " instead."));
      }
    }
  }
}

void TcpHeaderPrefixInvalidReasons(const TcpHeaderPrefix& header,
                                   const std::string& field_prefix,
                                   const Packet& packet, int header_index,
                                   std::vector<std::string>& output) {
  HexStringInvalidReasons<kUdpPortBitwidth>(
      header.source_port(), absl::StrCat(field_prefix, "source_port"), output);
  HexStringInvalidReasons<kUdpPortBitwidth>(
      header.destination_port(), absl::StrCat(field_prefix, "destination_port"),
      output);
}

}  // namespace

std::string HeaderCaseName(Header::HeaderCase header_case) {
  switch (header_case) {
    case Header::kEthernetHeader:
      return "EthernetHeader";
    case Header::kIpv4Header:
      return "Ipv4Header";
    case Header::kIpv6Header:
      return "Ipv6Header";
    case Header::kUdpHeader:
      return "UdpHeader";
    case Header::kTcpHeaderPrefix:
      return "TcpHeaderPrefix";
    case Header::HEADER_NOT_SET:
      return "HEADER_NOT_SET";
  }
  LOG(DFATAL) << "unexpected HeaderCase: " << header_case;
  return "";
}

std::vector<std::string> PacketInvalidReasons(const Packet& packet) {
  std::vector<std::string> result;

  if (auto bitsize = PacketSizeInBits(packet); !bitsize.ok()) {
    result.push_back(absl::StrCat("Unable to determine total packet size: ",
                                  bitsize.status().ToString()));
  } else if (*bitsize % 8 != 0) {
    result.push_back(absl::StrCat(
        "Packet size must be multiple of 8 bits; found ", *bitsize, " bits"));
  }

  Header::HeaderCase expected_header_case =
      packet.headers().empty() ? Header::HEADER_NOT_SET
                               : packet.headers(0).header_case();
  int index = -1;
  for (const Header& header : packet.headers()) {
    index += 1;
    const std::string header_prefix = absl::StrCat("headers[", index, "]: ");
    const std::string field_prefix = absl::StrCat("headers[", index, "].");

    switch (header.header_case()) {
      case Header::kEthernetHeader:
        EthernetHeaderInvalidReasons(header.ethernet_header(), field_prefix,
                                     packet, index, result);
        break;
      case Header::kIpv4Header:
        Ipv4HeaderInvalidReasons(header.ipv4_header(), field_prefix, packet,
                                 index, result);
        break;
      case Header::kIpv6Header:
        Ipv6HeaderInvalidReasons(header.ipv6_header(), field_prefix, packet,
                                 index, result);
        break;
      case Header::kUdpHeader: {
        UdpHeaderInvalidReasons(header.udp_header(), field_prefix, packet,
                                index, result);
        break;
      }
      case Header::kTcpHeaderPrefix: {
        TcpHeaderPrefixInvalidReasons(header.tcp_header_prefix(), field_prefix,
                                      packet, index, result);
        break;
      }
      case Header::HEADER_NOT_SET:
        result.push_back(absl::StrCat(header_prefix, "header uninitialized"));
        continue;  // skip expected_header_case check
    }

    // Check order of headers.
    if (expected_header_case == Header::HEADER_NOT_SET) {
      result.push_back(absl::StrCat(
          header_prefix,
          "expected no header (because the previous header demands either no "
          "header or an unsupported header), got ",
          HeaderCaseName(header.header_case())));
    } else if (header.header_case() != expected_header_case) {
      result.push_back(absl::StrCat(
          header_prefix, "expected ", HeaderCaseName(expected_header_case),
          " (because the previous header demands it), got ",
          HeaderCaseName(header.header_case())));
    }

    // Update `expected_header_case`.
    if (absl::StatusOr<NextHeader> next = GetNextHeader(header); next.ok()) {
      expected_header_case = absl::visit(
          gutil::Overload{
              [](Header::HeaderCase next) { return next; },
              [](UnsupportedNextHeader) { return Header::HEADER_NOT_SET; }},
          *next);
    } else {
      expected_header_case = Header::HEADER_NOT_SET;
    }
  }

  if (expected_header_case != Header::HEADER_NOT_SET) {
    result.push_back(absl::StrCat("headers[", packet.headers().size(),
                                  "]: header missing - expected ",
                                  HeaderCaseName(expected_header_case)));
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
absl::Status SerializeIpv6Address(absl::string_view address,
                                  pdpi::BitString& output) {
  ASSIGN_OR_RETURN(Ipv6Address parsed_address, Ipv6Address::OfString(address));
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
  RETURN_IF_ERROR(SerializeMacAddress(header.ethernet_destination(), output));
  RETURN_IF_ERROR(SerializeMacAddress(header.ethernet_source(), output));
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
  if (!header.uninterpreted_options().empty()) {
    RETURN_IF_ERROR(output.AppendHexString(header.uninterpreted_options()));
  }
  return absl::OkStatus();
}

absl::Status SerializeIpv6Header(const Ipv6Header& header,
                                 pdpi::BitString& output) {
  RETURN_IF_ERROR(SerializeBits<kIpVersionBitwidth>(header.version(), output));
  RETURN_IF_ERROR(SerializeBits<kIpDscpBitwidth>(header.dscp(), output));
  RETURN_IF_ERROR(SerializeBits<kIpEcnBitwidth>(header.ecn(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpFlowLabelBitwidth>(header.flow_label(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpPayloadLengthBitwidth>(header.payload_length(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpNextHeaderBitwidth>(header.next_header(), output));
  RETURN_IF_ERROR(
      SerializeBits<kIpHopLimitBitwidth>(header.hop_limit(), output));
  RETURN_IF_ERROR(SerializeIpv6Address(header.ipv6_source(), output));
  RETURN_IF_ERROR(SerializeIpv6Address(header.ipv6_destination(), output));
  return absl::OkStatus();
}

absl::Status SerializeUdpHeader(const UdpHeader& header,
                                pdpi::BitString& output) {
  RETURN_IF_ERROR(
      SerializeBits<kUdpPortBitwidth>(header.source_port(), output));
  RETURN_IF_ERROR(
      SerializeBits<kUdpPortBitwidth>(header.destination_port(), output));
  RETURN_IF_ERROR(SerializeBits<kUdpLengthBitwidth>(header.length(), output));
  RETURN_IF_ERROR(
      SerializeBits<kUdpChecksumBitwidth>(header.checksum(), output));
  return absl::OkStatus();
}

absl::Status SerializeTcpHeaderPrefix(const TcpHeaderPrefix& header,
                                      pdpi::BitString& output) {
  RETURN_IF_ERROR(
      SerializeBits<kTcpPortBitwidth>(header.source_port(), output));
  RETURN_IF_ERROR(
      SerializeBits<kTcpPortBitwidth>(header.destination_port(), output));
  return absl::OkStatus();
}

absl::Status SerializeHeader(const Header& header, pdpi::BitString& output) {
  switch (header.header_case()) {
    case Header::kEthernetHeader:
      return SerializeEthernetHeader(header.ethernet_header(), output);
    case Header::kIpv4Header:
      return SerializeIpv4Header(header.ipv4_header(), output);
    case Header::kIpv6Header:
      return SerializeIpv6Header(header.ipv6_header(), output);
    case Header::kUdpHeader:
      return SerializeUdpHeader(header.udp_header(), output);
    case Header::kTcpHeaderPrefix:
      return SerializeTcpHeaderPrefix(header.tcp_header_prefix(), output);
    case Header::HEADER_NOT_SET:
      return gutil::InvalidArgumentErrorBuilder()
             << "Found invalid HEADER_NOT_SET in header.";
  }
}

}  // namespace

absl::Status RawSerializePacket(const Packet& packet, int start_header_index,
                                pdpi::BitString& output) {
  if (start_header_index > packet.headers_size() || start_header_index < 0) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid header index " << start_header_index
           << " for a packet with " << packet.headers_size() << " headers.";
  }

  for (int i = start_header_index; i < packet.headers().size(); ++i) {
    RETURN_IF_ERROR(SerializeHeader(packet.headers(i), output)).SetPrepend()
        << "while trying to serialize packet.headers(" << i << "): ";
  }
  if (!packet.payload().empty()) {
    RETURN_IF_ERROR(output.AppendHexString(packet.payload())).SetPrepend()
        << "while trying to serialze packet.payload: ";
  }
  return absl::OkStatus();
}

absl::StatusOr<std::string> RawSerializePacket(const Packet& packet) {
  pdpi::BitString bits;
  RETURN_IF_ERROR(RawSerializePacket(packet, 0, bits));
  return bits.ToByteString();
}

absl::StatusOr<std::string> SerializePacket(Packet packet) {
  RETURN_IF_ERROR(UpdateComputedFields(packet).status());
  RETURN_IF_ERROR(ValidatePacket(packet));
  return RawSerializePacket(packet);
}

// ---- Computed field logic ---------------------------------------------------

absl::StatusOr<bool> UpdateComputedFields(Packet& packet) {
  bool changes = false;

  int header_index = 0;
  for (Header& header : *packet.mutable_headers()) {
    const std::string error_prefix =
        absl::StrFormat("failed to compute packet.headers[%d].", header_index);
    switch (header.header_case()) {
      case Header::kIpv4Header: {
        Ipv4Header& ipv4_header = *header.mutable_ipv4_header();
        if (ipv4_header.version().empty()) {
          ipv4_header.set_version("0x4");
          changes = true;
        }
        if (ipv4_header.ihl().empty()) {
          absl::string_view options = ipv4_header.uninterpreted_options();
          if (options.empty()) {
            ipv4_header.set_ihl("0x5");
            changes = true;
          } else if (absl::ConsumePrefix(&options, "0x") &&
                     (options.size() * 4) % 32 == 0) {
            // 4 bits per hex char.
            int num_32bit_words_in_options = (options.size() * 4) / 32;
            ipv4_header.set_ihl(pdpi::BitsetToHexString<kIpIhlBitwidth>(
                5 + num_32bit_words_in_options));
            changes = true;
          } else {
            return gutil::InvalidArgumentErrorBuilder()
                   << error_prefix
                   << "ihl: uninterpreted_options field is invalid";
          }
        }
        if (ipv4_header.total_length().empty()) {
          ASSIGN_OR_RETURN(int size, PacketSizeInBytes(packet, header_index),
                           _.SetPrepend() << error_prefix << "total_length: ");
          ipv4_header.set_total_length(pdpi::BitsetToHexString(
              std::bitset<kIpTotalLengthBitwidth>(size)));
          changes = true;
        }
        if (ipv4_header.checksum().empty()) {
          ASSIGN_OR_RETURN(int checksum, Ipv4HeaderChecksum(ipv4_header),
                           _.SetPrepend() << error_prefix << "checksum: ");
          ipv4_header.set_checksum(pdpi::BitsetToHexString(
              std::bitset<kIpChecksumBitwidth>(checksum)));
          changes = true;
        }
        break;
      }
      case Header::kIpv6Header: {
        Ipv6Header& ipv6_header = *header.mutable_ipv6_header();
        if (ipv6_header.version().empty()) {
          ipv6_header.set_version("0x6");
          changes = true;
        }
        if (ipv6_header.payload_length().empty()) {
          // `+1` to skip the IPv6 header and previous headers in calculation.
          ASSIGN_OR_RETURN(
              int size, PacketSizeInBytes(packet, header_index + 1),
              _.SetPrepend() << error_prefix << "payload_length: ");
          ipv6_header.set_payload_length(pdpi::BitsetToHexString(
              std::bitset<kIpTotalLengthBitwidth>(size)));
          changes = true;
        }
        break;
      }
      case Header::kUdpHeader: {
        UdpHeader& udp_header = *header.mutable_udp_header();
        if (udp_header.length().empty()) {
          ASSIGN_OR_RETURN(int size, PacketSizeInBytes(packet, header_index),
                           _.SetPrepend() << error_prefix << "length: ");
          udp_header.set_length(
              pdpi::BitsetToHexString(std::bitset<kUdpLengthBitwidth>(size)));
          changes = true;
        }
        if (udp_header.checksum().empty()) {
          ASSIGN_OR_RETURN(int checksum,
                           UdpHeaderChecksum(packet, header_index),
                           _.SetPrepend() << error_prefix << "checksum: ");
          udp_header.set_checksum(pdpi::BitsetToHexString(
              std::bitset<kUdpChecksumBitwidth>(checksum)));
          changes = true;
        }
        break;
      }
      case Header::kEthernetHeader:
      case Header::kTcpHeaderPrefix:
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

absl::StatusOr<int> PacketSizeInBytes(const Packet& packet,
                                      int start_header_index) {
  ASSIGN_OR_RETURN(int bit_size, PacketSizeInBits(packet, start_header_index));
  if (bit_size % 8 != 0) {
    return gutil::InvalidArgumentErrorBuilder()
           << "packet size of " << bit_size << " cannot be converted to bytes";
  }
  return bit_size / 8;
}

absl::StatusOr<int> PacketSizeInBits(const Packet& packet,
                                     int start_header_index) {
  if (start_header_index > packet.headers_size() || start_header_index < 0) {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid header index " << start_header_index
           << " for a packet with " << packet.headers_size() << " headers.";
  }

  int size = 0;

  for (auto* header :
       absl::MakeSpan(packet.headers()).subspan(start_header_index)) {
    switch (header->header_case()) {
      case Header::kEthernetHeader:
        size += kEthernetHeaderBitwidth;
        break;
      case Header::kIpv4Header: {
        size += kStandardIpv4HeaderBitwidth;
        if (const auto& options = header->ipv4_header().uninterpreted_options();
            !options.empty()) {
          ASSIGN_OR_RETURN(
              auto bytes, pdpi::HexStringToByteString(options),
              _.SetPrepend()
                  << "failed to parse uninterpreted_options in Ipv4Header: ");
          size += 8 * bytes.size();
        }
        break;
      }
      case Header::kIpv6Header:
        size += kIpv6HeaderBitwidth;
        break;
      case Header::kUdpHeader:
        size += kUdpHeaderBitwidth;
        break;
      case Header::kTcpHeaderPrefix:
        size += kTcpHeaderPrefixBitwidth;
        break;
      case Header::HEADER_NOT_SET:
        return gutil::InvalidArgumentErrorBuilder()
               << "Found invalid HEADER_NOT_SET in header.";
    }
  }

  if (!packet.payload().empty()) {
    // 4 bits for every hex char after the '0x' prefix.
    size += 4 * (packet.payload().size() - 2);
  }

  return size;
}

// Returns 16-bit ones' complement of the ones' complement sum of all 16-bit
// words in the given BitString.
static absl::StatusOr<int> OnesComplementChecksum(pdpi::BitString data) {
  // Pad string to be 16-bit multiple.
  while (data.size() % 16 != 0) data.AppendBit(0);

  // Following RFC 1071 and
  // wikipedia.org/wiki/IPv4_header_checksum#Calculating_the_IPv4_header_checksum
  int sum = 0;
  while (data.size() != 0) {
    ASSIGN_OR_RETURN(std::bitset<16> word, data.ConsumeBitset<16>(),
                     _.SetCode(absl::StatusCode::kInternal));
    // This looks wrong because we're not taking the ones' complement, but turns
    // out to work.
    sum += word.to_ulong();
  }
  // Add carry bits until sum fits into 16 bits.
  while (sum >> 16 != 0) {
    sum = (sum & 0xffff) + (sum >> 16);
  }
  // Return 16 bit ones' complement.
  return (~sum) & 0xffff;
}

absl::StatusOr<int> Ipv4HeaderChecksum(Ipv4Header header) {
  // The checksum field is the 16-bit ones' complement of the ones' complement
  // sum of all 16-bit words in the header. For purposes of computing the
  // checksum, the value of the checksum field is zero.

  // We compute the checksum by setting the checksum field to 0, serializing the
  // header, and then going over all 16-bit words.
  header.set_checksum("0x0000");
  pdpi::BitString data;
  RETURN_IF_ERROR(SerializeIpv4Header(header, data));
  return OnesComplementChecksum(std::move(data));
}

absl::StatusOr<int> UdpHeaderChecksum(Packet packet, int udp_header_index) {
  auto invalid_argument = gutil::InvalidArgumentErrorBuilder()
                          << "UdpHeaderChecksum(packet, udp_header_index = "
                          << udp_header_index << "): ";
  if (udp_header_index < 1 || udp_header_index >= packet.headers().size()) {
    return invalid_argument
           << "udp_header_index must be in [1, " << packet.headers().size()
           << ") since the given packet has " << packet.headers().size()
           << " headers and the UDP header must be preceded by an IP header";
  }
  const Header& preceding_header = packet.headers(udp_header_index - 1);
  if (auto header_case = packet.headers(udp_header_index).header_case();
      header_case != Header::kUdpHeader) {
    return invalid_argument << "packet.headers[" << udp_header_index
                            << "] is a " << HeaderCaseName(header_case)
                            << ", expected UdpHeader";
  }
  UdpHeader& udp_header =
      *packet.mutable_headers(udp_header_index)->mutable_udp_header();
  udp_header.set_checksum("0x0000");

  // Serialize "pseudo header" for checksum calculation, following
  // https://en.wikipedia.org/wiki/User_Datagram_Protocol#Checksum_computation.
  pdpi::BitString data;
  switch (preceding_header.header_case()) {
    case Header::kIpv4Header: {
      auto& header = preceding_header.ipv4_header();
      RETURN_IF_ERROR(SerializeIpv4Address(header.ipv4_source(), data));
      RETURN_IF_ERROR(SerializeIpv4Address(header.ipv4_destination(), data));
      data.AppendBits<8>(0);
      RETURN_IF_ERROR(
          SerializeBits<kIpProtocolBitwidth>(header.protocol(), data));
      RETURN_IF_ERROR(
          SerializeBits<kUdpLengthBitwidth>(udp_header.length(), data));
      break;
    }
    case Header::kIpv6Header: {
      auto& header = preceding_header.ipv6_header();
      RETURN_IF_ERROR(SerializeIpv6Address(header.ipv6_source(), data));
      RETURN_IF_ERROR(SerializeIpv6Address(header.ipv6_destination(), data));
      data.AppendBits<16>(0);
      RETURN_IF_ERROR(
          SerializeBits<kUdpLengthBitwidth>(udp_header.length(), data));
      data.AppendBits<24>(0);
      RETURN_IF_ERROR(
          SerializeBits<kIpNextHeaderBitwidth>(header.next_header(), data));
      break;
    }
    default:
      return invalid_argument << "expected packet.headers[udp_header_index - "
                                 "1] to be an IP header, got "
                              << HeaderCaseName(preceding_header.header_case());
  }
  RETURN_IF_ERROR(RawSerializePacket(packet, udp_header_index, data));
  return OnesComplementChecksum(std::move(data));
}

}  // namespace packetlib

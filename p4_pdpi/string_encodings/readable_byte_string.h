#ifndef GOOGLE_P4_PDPI_PACKETLIB_READABLE_BIT_STRING_H_
#define GOOGLE_P4_PDPI_PACKETLIB_READABLE_BIT_STRING_H_

#include "absl/status/statusor.h"

namespace pdpi {

// Library to write down byte strings in a readable manner. This is useful e.g.
// for writing down network packets in a readable manner.
//
// Example:
// R"PB(
//   # ethernet header
//   ethernet_source: 0x112233445566
//   ethernet_destination: 0xaabbccddeeff
//   ether_type: 0x0800
//   # IPv4 header:
//   version: 0x4
//   ihl: 0x5
//   dhcp: 0b011011)PB"
//
// Supports comments (using #), annotations of what a group of bits represents
// (string before the colon), hex strings, base-2 strings.
absl::StatusOr<std::string> ReadableByteStringToByteString(
    absl::string_view readable_byte_string);

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_PACKETLIB_READABLE_BIT_STRING_H_

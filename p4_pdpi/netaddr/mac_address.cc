#include "p4_pdpi/netaddr/mac_address.h"

#include <bitset>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gutil/status.h"
#include "p4_pdpi/netaddr/network_address.h"

namespace netaddr {

namespace {

bool ParseByteInBase16(absl::string_view base16_string, uint8_t& byte) {
  if (base16_string.empty() || base16_string.size() > 2) return false;
  int buffer = 0;
  for (char c : base16_string) {
    if (!absl::ascii_isxdigit(c)) return false;
    int value =
        (c >= 'A') ? (c >= 'a') ? (c - 'a' + 10) : (c - 'A' + 10) : (c - '0');
    buffer = buffer * 16 + value;
  }
  memcpy(&byte, &buffer, 1);
  return true;
}

}  // namespace

absl::StatusOr<MacAddress> MacAddress::OfString(absl::string_view address) {
  auto invalid = [=]() {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid MAC address: " << address;
  };

  std::vector<std::string> bytes = absl::StrSplit(address, ':');
  if (bytes.size() != 6) return invalid();

  std::bitset<48> bits;
  for (absl::string_view byte_string : bytes) {
    uint8_t byte;
    if (!ParseByteInBase16(byte_string, byte)) return invalid();
    bits <<= 8;
    bits |= byte;
  }
  return MacAddress(bits);
}

std::string MacAddress::ToString() const {
  uint8_t byte6 = (bits_ >> 40).to_ulong() & 0xFFu;
  uint8_t byte5 = (bits_ >> 32).to_ulong() & 0xFFu;
  uint8_t byte4 = (bits_ >> 24).to_ulong() & 0xFFu;
  uint8_t byte3 = (bits_ >> 16).to_ulong() & 0xFFu;
  uint8_t byte2 = (bits_ >> 8).to_ulong() & 0xFFu;
  uint8_t byte1 = (bits_ >> 0).to_ulong() & 0xFFu;
  return absl::StrFormat("%02x:%02x:%02x:%02x:%02x:%02x", byte6, byte5, byte4,
                         byte3, byte2, byte1);
}

}  // namespace netaddr

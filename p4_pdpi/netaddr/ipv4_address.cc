#include "p4_pdpi/netaddr/ipv4_address.h"

#include <bitset>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "gutil/status.h"
#include "p4_pdpi/netaddr/network_address.h"

namespace netaddr {

namespace {

bool ParseDecimalByte(absl::string_view decimal_string, uint8_t& byte) {
  if (decimal_string.empty() || decimal_string.size() > 3) return false;
  int buffer = 0;
  for (; !decimal_string.empty(); decimal_string.remove_prefix(1)) {
    char digit = decimal_string[0] - '0';
    if (digit > 10u) return false;  // Not a decimal digit.
    buffer = buffer * 10 + digit;
  }
  if (buffer > 255) return false;  // Too large to fit into a byte.
  memcpy(&byte, &buffer, 1);
  return true;
}

}  // namespace

absl::StatusOr<Ipv4Address> Ipv4Address::OfString(absl::string_view address) {
  auto invalid = [=]() {
    return gutil::InvalidArgumentErrorBuilder()
           << "Invalid IPv4 address: " << address;
  };

  std::vector<std::string> bytes = absl::StrSplit(address, '.');
  if (bytes.size() != 4) return invalid();

  std::bitset<32> bits;
  for (absl::string_view byte_string : bytes) {
    uint8_t byte;
    if (!ParseDecimalByte(byte_string, byte)) return invalid();
    bits <<= 8;
    bits |= byte;
  }
  return Ipv4Address(bits);
}

std::string Ipv4Address::ToString() const {
  uint8_t byte4 = (bits_ >> 24).to_ulong() & 0xFFu;
  uint8_t byte3 = (bits_ >> 16).to_ulong() & 0xFFu;
  uint8_t byte2 = (bits_ >> 8).to_ulong() & 0xFFu;
  uint8_t byte1 = (bits_ >> 0).to_ulong() & 0xFFu;
  return absl::StrFormat("%d.%d.%d.%d", byte4, byte3, byte2, byte1);
}

}  // namespace netaddr

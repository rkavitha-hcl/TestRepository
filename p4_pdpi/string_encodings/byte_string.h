// Conversions to and from byte strings in network byte order (big endian).
//
// NOTE: By convention, byte strings are always nonempty, and functions
// consuming byte strings must reject the empty string. This is to catch
// uninitialized byte strings, e.g. in protobuf messages.
//
// There are 2 flavors of byte strings used in this file:
//
// 1. Padded Byte String: Uses exactly ceil(n/8) characters to encode n bits,
//    padding with zeros as necessary.
//
// 2. P4Runtime Byte String: Omits leading zeros. This is the "canonical binary
//    string representation" used by P4RT, see
//    https://p4.org/p4runtime/spec/master/P4Runtime-Spec.html#sec-bytestrings.

#ifndef GOOGLE_P4_PDPI_STRING_ENCODINGS_BYTE_STRING_H_
#define GOOGLE_P4_PDPI_STRING_ENCODINGS_BYTE_STRING_H_

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <string>

#include "absl/status/statusor.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "gutil/status.h"
#include "p4_pdpi/utils/ir.h"

namespace pdpi {

// Reads bits from arbitrary-size, nonempty byte string.
// Missing bits are assumed to be zero.
// Extra bits are checked to be zero, returning error status otherwise.
template <std::size_t num_bits>
absl::StatusOr<std::bitset<num_bits>> ByteStringToBitset(
    absl::string_view byte_string);

// Writes the given bits to a zero-padded byte string of size ceil(bits/8).
template <std::size_t num_bits>
std::string BitsetToPaddedByteString(std::bitset<num_bits> bits);

// Writes the given bits to a canonical P4Runtime binary string.
template <std::size_t num_bits>
std::string BitsetToP4RuntimeByteString(std::bitset<num_bits> bits);

// == END OF PUBLIC INTERFACE ==================================================

namespace internal {

// Returns the number of bytes needed to encode the given number of bits.
inline constexpr int NumBitsToNumBytes(int num_bits) {
  return (num_bits + 7) / 8;  // ceil(num_bits/8)
}

template <std::size_t num_bits>
std::bitset<num_bits> AnyByteStringToBitset(absl::string_view byte_string) {
  std::bitset<num_bits> bits;
  for (char byte : byte_string) {
    bits <<= 8;
    bits |= byte;
  }
  return bits;
}

}  // namespace internal

template <std::size_t num_bits>
absl::StatusOr<std::bitset<num_bits>> ByteStringToBitset(
    absl::string_view byte_string) {
  if (byte_string.empty()) {
    return absl::InvalidArgumentError("byte string must be nonempty");
  }

  auto invalid = [&] {
    return gutil::InvalidArgumentErrorBuilder()
           << "cannot fit given byte string into " << num_bits
           << " bits: " << absl::BytesToHexString(byte_string);
  };
  constexpr int kNumRelevantBytes = internal::NumBitsToNumBytes(num_bits);
  const int num_extra_bytes =
      static_cast<int>(byte_string.size()) - kNumRelevantBytes;
  if (num_extra_bytes >= 0) {
    for (char extra_byte : byte_string.substr(0, num_extra_bytes)) {
      if (extra_byte != 0) return invalid();
    }
    byte_string.remove_prefix(num_extra_bytes);

    if (num_bits % 8 != 0) {
      char extra_bits = byte_string[0] >> (num_bits % 8);
      if (extra_bits != 0) return invalid();
    }
  }

  return internal::AnyByteStringToBitset<num_bits>(byte_string);
}

template <std::size_t num_bits>
std::string BitsetToPaddedByteString(std::bitset<num_bits> bits) {
  static constexpr int kNumBytes = internal::NumBitsToNumBytes(num_bits);

  std::string byte_string;
  byte_string.reserve(kNumBytes);
  for (int i = 0; i < kNumBytes; ++i) {
    char byte = bits.to_ulong() & 0xFFu;
    byte_string.push_back(byte);
    bits >>= 8;
  }
  std::reverse(byte_string.begin(), byte_string.end());
  return byte_string;
}

template <std::size_t num_bits>
std::string BitsetToP4RuntimeByteString(std::bitset<num_bits> bits) {
  return NormalizedToCanonicalByteString(BitsetToPaddedByteString(bits));
}

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_STRING_ENCODINGS_BYTE_STRING_H_

// Safe char and string construction from integers.

#ifndef GOOGLE_P4_PDPI_STRING_ENCODINGS_SAFE_H_
#define GOOGLE_P4_PDPI_STRING_ENCODINGS_SAFE_H_

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pdpi {

// Safely constructs `char` from integer. Example: `SafeChar(0xFF)`.
//
// In C(++), it is implementation dependent whether char is signed or unsigned.
// This can lead to surprises/undefined behavior when relying on implicit
// conversions from integers types to char. This function avoids such surprises.
char SafeChar(uint8_t byte);

// Safely constructs a string from a vector of bytes specified as integers.
// For example: `SafeString({0xFF, 0x10})`.
std::string SafeString(std::vector<uint8_t> bytes);

}  // namespace pdpi

#endif  // GOOGLE_P4_PDPI_STRING_ENCODINGS_SAFE_H_

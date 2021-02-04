#include "p4_pdpi/string_encodings/byte_string.h"

#include <string>

namespace pdpi {

std::string ByteStringToP4runtimeByteString(std::string bytes) {
  // Remove leading zeros.
  bytes.erase(0, std::min(bytes.find_first_not_of('\x00'), bytes.size() - 1));
  return bytes;
}

}  // namespace pdpi

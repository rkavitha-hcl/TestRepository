#include "p4_pdpi/string_encodings/safe.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace pdpi {

char SafeChar(uint8_t byte) {
  char c = 0;
  memcpy(&c, &byte, 1);
  return c;
}

std::string SafeString(std::vector<uint8_t> bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (auto byte : bytes) result.push_back(SafeChar(byte));
  return result;
}

}  // namespace pdpi

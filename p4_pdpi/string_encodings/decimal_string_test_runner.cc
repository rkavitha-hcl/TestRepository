#include "p4_pdpi/string_encodings/decimal_string.h"

#define TEST_STATUSOR(function_call)                      \
  do {                                                    \
    std::cout << "$ " << #function_call << "\n-> ";       \
    if (auto status_or = function_call; status_or.ok()) { \
      std::cout << status_or.value();                     \
    } else {                                              \
      std::cout << "error: " << status_or.status();       \
    }                                                     \
    std::cout << "\n\n";                                  \
  } while (false)

int main() {
  TEST_STATUSOR(pdpi::DecimalStringToInt("0123"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("x123"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("-123"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("+123"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("2147483648"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("2147483650"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("0x1112323"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("2147483647"));
  TEST_STATUSOR(pdpi::DecimalStringToInt("0"));

  TEST_STATUSOR(pdpi::DecimalStringToInt64("9223372036854775807"));
  TEST_STATUSOR(pdpi::DecimalStringToInt64("9223372036854775808"));

  TEST_STATUSOR(pdpi::DecimalStringToInt32("2147483648"));
  TEST_STATUSOR(pdpi::DecimalStringToInt32("2147483647"));
  TEST_STATUSOR(pdpi::DecimalStringToUint32("4294967295"));
  TEST_STATUSOR(pdpi::DecimalStringToUint32("4294967296"));

  TEST_STATUSOR(pdpi::DecimalStringToUint64("18446744073709551615"));
  TEST_STATUSOR(pdpi::DecimalStringToUint64("18446744073709551616"));

  TEST_STATUSOR(pdpi::IntToDecimalString(-1));
  // decimal literal.
  TEST_STATUSOR(pdpi::IntToDecimalString(213));
  TEST_STATUSOR(pdpi::IntToDecimalString(2147483648));
  TEST_STATUSOR(pdpi::IntToDecimalString(4294967296));
  TEST_STATUSOR(pdpi::IntToDecimalString(9223372036854775807));
  TEST_STATUSOR(pdpi::IntToDecimalString(18446744073709551615U));
  // octal literal.
  TEST_STATUSOR(pdpi::IntToDecimalString(0213));
  // hexadecimal literal.
  TEST_STATUSOR(pdpi::IntToDecimalString(0x213));
}

// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// This file is responsible for parsing values from the bmv2 json and table
// entries.
// It is also responsible for translating any string values to corresponding
// bitvectors and back, for fields that have the @p4runtime_translation
// annotation.

#include "p4_symbolic/symbolic/values.h"

#include <locale>
#include <sstream>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gmpxx.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "p4_pdpi/netaddr/ipv6_address.h"
#include "p4_pdpi/netaddr/mac_address.h"
#include "p4_pdpi/utils/ir.h"
#include "p4_symbolic/symbolic/operators.h"
#include "p4_symbolic/symbolic/symbolic.h"

namespace p4_symbolic {
namespace symbolic {
namespace values {

namespace {

// Finds the minimum bit size required for representing the given value.
unsigned int FindBitsize(uint64_t value) {
  unsigned int bitsize = 0;
  uint64_t pow = 1;
  while (bitsize <= 64 && pow <= value) {
    pow = pow * 2;
    bitsize++;
  }
  return (bitsize > 1 ? bitsize : 1);  // At least 1 bit.
}

// Turns the given z3 extracted value (as a string) to a uint64_t.
// Z3 returns an extracted value as either a binary, hex, or int strings
// dependening on the size of the value and the formatting flags it is
// initialized with.
uint64_t StringToInt(std::string value) {
  static std::unordered_map<char, std::string> hex_to_bin = {
      {'0', "0000"}, {'1', "0001"}, {'2', "0010"}, {'3', "0011"},
      {'4', "0100"}, {'5', "0101"}, {'6', "0110"}, {'7', "0111"},
      {'8', "1000"}, {'9', "1001"}, {'a', "1010"}, {'b', "1011"},
      {'c', "1100"}, {'d', "1101"}, {'e', "1110"}, {'f', "1111"}};

  bool value_is_hex = absl::StartsWith(value, "#x");
  bool value_is_binary = absl::StartsWith(value, "#b");

  // Boolean or integer values.
  if (!value_is_hex && !value_is_binary) {
    if (value == "true") {
      return 1;
    } else if (value == "false") {
      return 0;
    } else {
      return std::stoull(value);
    }
  }

  // Make sure value is a binary string without leading base prefix.
  std::string binary;
  if (value_is_hex) {
    // Turn hex to binary.
    absl::string_view stripped_value = absl::StripPrefix(value, "#x");
    for (char c : stripped_value) {
      absl::StrAppend(&binary, hex_to_bin.at(c));
    }
  } else if (value_is_binary) {
    // Strip leading #b for binary strings.
    binary = absl::StripPrefix(value, "#b");
  }

  return std::stoull(binary);
}

z3::expr HexStringToZ3Expr(const std::string &hex_string,
                           absl::optional<int> bitwidth = absl::nullopt) {
  mpz_class integer = mpz_class(hex_string, /*base=*/0);
  std::string decimal = integer.get_str(/*base=*/10);
  if (!bitwidth.has_value()) {
    bitwidth = integer.get_str(/*base=*/2).size();
  }
  return Z3Context().bv_val(decimal.c_str(), *bitwidth);
}

}  // namespace

absl::StatusOr<pdpi::IrValue> ParseIrValue(std::string value) {
  // Format according to type.
  if (absl::StartsWith(value, "0x")) {
    return pdpi::FormattedStringToIrValue(value, pdpi::Format::HEX_STRING);
  } else {
    // Some unsupported format!
    return absl::InvalidArgumentError(
        absl::StrCat("Literal value \"", value, "\" has unknown format!"));
  }
}

absl::StatusOr<z3::expr> FormatBmv2Value(const pdpi::IrValue &value) {
  switch (value.format_case()) {
    case pdpi::IrValue::kHexStr:
      return HexStringToZ3Expr(value.hex_str());
    case pdpi::IrValue::kIpv4: {
      ASSIGN_OR_RETURN(netaddr::Ipv4Address ipv4,
                       netaddr::Ipv4Address::OfString(value.ipv4()));
      return HexStringToZ3Expr(ipv4.ToHexString(), 32);
    }
    case pdpi::IrValue::kIpv6: {
      ASSIGN_OR_RETURN(netaddr::Ipv6Address ipv6,
                       netaddr::Ipv6Address::OfString(value.ipv6()));
      return HexStringToZ3Expr(ipv6.ToHexString(), 128);
    }
    case pdpi::IrValue::kMac: {
      ASSIGN_OR_RETURN(netaddr::MacAddress mac,
                       netaddr::MacAddress::OfString(value.mac()));
      return HexStringToZ3Expr(mac.ToHexString(), 48);
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Found unsupported value type ", value.DebugString()));
  }
}

absl::StatusOr<z3::expr> FormatP4RTValue(const std::string &field_name,
                                         const std::string &type_name,
                                         const pdpi::IrValue &value,
                                         P4RuntimeTranslator *translator) {
  switch (value.format_case()) {
    case pdpi::IrValue::kStr: {
      // Mark that this field is a string translatable field, and map it
      // to its custom type name (e.g. vrf_id => vrf_t).
      if (!field_name.empty()) {
        translator->fields_p4runtime_type[field_name] = type_name;
      }

      // Must translate the string into a bitvector according to the field type.
      const std::string &string_value = value.str();
      IdAllocator &allocator =
          translator->p4runtime_translation_allocators[type_name];
      uint64_t int_value = allocator.AllocateId(string_value);
      return Z3Context().bv_val(int_value, FindBitsize(int_value));
    }
    default: {
      if (translator->fields_p4runtime_type.count(field_name)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "A table entry provides a non-string value ", value.DebugString(),
            "to a string translated field", field_name));
      }
      return FormatBmv2Value(value);
    }
  }
}

absl::StatusOr<std::string> TranslateValueToP4RT(
    const std::string &field_name, const std::string &value,
    const P4RuntimeTranslator &translator) {
  // Not translatable: identity function.
  if (!translator.fields_p4runtime_type.count(field_name)) {
    return value;
  }

  // Translatable: do the reverse translation via the type name.
  const std::string &field_type_name =
      translator.fields_p4runtime_type.at(field_name);
  const IdAllocator &allocator =
      translator.p4runtime_translation_allocators.at(field_type_name);

  // Turn the value from a string to an int.
  uint64_t int_value = StringToInt(value);
  return allocator.IdToString(int_value);
}

// IdAllocator Implementation.

uint64_t IdAllocator::AllocateId(const std::string &string_value) {
  // If previously allocated, return the same bitvector value.
  if (this->string_to_id_map_.count(string_value)) {
    return this->string_to_id_map_.at(string_value);
  }

  // Allocate new bitvector value and store it in mapping.
  uint64_t int_value = this->counter_++;
  this->string_to_id_map_.insert({string_value, int_value});
  this->id_to_string_map_.insert({int_value, string_value});
  return int_value;
}

absl::StatusOr<std::string> IdAllocator::IdToString(uint64_t value) const {
  // Normalize the bitvector and look it up in the reverse mapping.
  if (this->id_to_string_map_.count(value)) {
    return this->id_to_string_map_.at(value);
  }

  // Could not find the bitvector in reverse map!
  return absl::InvalidArgumentError(
      absl::StrCat("Cannot translate bitvector ", value, " to a string value"));
}

}  // namespace values
}  // namespace symbolic
}  // namespace p4_symbolic

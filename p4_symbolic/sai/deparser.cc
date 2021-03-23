#include "p4_symbolic/sai/deparser.h"

#include <stddef.h>

#include <bitset>
#include <string>
#include <type_traits>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "gutil/status.h"
#include "p4_pdpi/string_encodings/bit_string.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "p4_symbolic/sai/fields.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "z3++.h"

namespace p4_symbolic {

namespace {

template <size_t num_bits>
absl::StatusOr<std::bitset<num_bits>> EvalBitvector(const z3::expr& bv_expr,
                                                    const z3::model& model) {
  if (!bv_expr.is_bv() || bv_expr.get_sort().bv_size() != num_bits) {
    return gutil::InvalidArgumentErrorBuilder()
           << "expected bitvector of " << num_bits << " bits, but got "
           << bv_expr.get_sort() << ": " << bv_expr;
  }

  std::string value_with_prefix = model.eval(bv_expr, true).to_string();
  absl::string_view value = value_with_prefix;
  if (absl::ConsumePrefix(&value, "#x")) {
    return pdpi::HexStringToBitset<num_bits>(absl::StrCat("0x", value));
  }
  if (absl::ConsumePrefix(&value, "#b")) {
    return std::bitset<num_bits>(std::string(value));
  }
  return gutil::InvalidArgumentErrorBuilder()
         << "invalid Z3 bitvector value '" << value_with_prefix << "'";
}

template <size_t num_bits>
absl::Status Deparse(const z3::expr& field, const z3::model& model,
                     pdpi::BitString& result) {
  ASSIGN_OR_RETURN(std::bitset<num_bits> bits,
                   EvalBitvector<num_bits>(field, model));
  result.AppendBits(bits);
  return absl::OkStatus();
}

absl::Status Deparse(const SaiEthernet& header, const z3::model& model,
                     pdpi::BitString& result) {
  if (model.eval(header.valid, true).bool_value()) {
    RETURN_IF_ERROR(Deparse<48>(header.dst_addr, model, result));
    RETURN_IF_ERROR(Deparse<48>(header.src_addr, model, result));
    RETURN_IF_ERROR(Deparse<16>(header.ether_type, model, result));
  }
  return absl::OkStatus();
}

absl::Status Deparse(const SaiIpv4& header, const z3::model& model,
                     pdpi::BitString& result) {
  if (model.eval(header.valid, true).bool_value()) {
    RETURN_IF_ERROR(Deparse<4>(header.version, model, result));
    RETURN_IF_ERROR(Deparse<4>(header.ihl, model, result));
    RETURN_IF_ERROR(Deparse<6>(header.dscp, model, result));
    RETURN_IF_ERROR(Deparse<2>(header.ecn, model, result));
    RETURN_IF_ERROR(Deparse<16>(header.total_len, model, result));
    RETURN_IF_ERROR(Deparse<16>(header.identification, model, result));
    RETURN_IF_ERROR(Deparse<1>(header.reserved, model, result));
    RETURN_IF_ERROR(Deparse<1>(header.do_not_fragment, model, result));
    RETURN_IF_ERROR(Deparse<1>(header.more_fragments, model, result));
    RETURN_IF_ERROR(Deparse<13>(header.frag_offset, model, result));
    RETURN_IF_ERROR(Deparse<8>(header.ttl, model, result));
    RETURN_IF_ERROR(Deparse<8>(header.protocol, model, result));
    RETURN_IF_ERROR(Deparse<16>(header.header_checksum, model, result));
    RETURN_IF_ERROR(Deparse<32>(header.src_addr, model, result));
    RETURN_IF_ERROR(Deparse<32>(header.dst_addr, model, result));
  }
  return absl::OkStatus();
}

}  // namespace

absl::StatusOr<pdpi::BitString> SaiDeparser(
    const symbolic::SymbolicPerPacketState& packet, const z3::model& model) {
  ASSIGN_OR_RETURN(SaiFields fields, GetSaiFields(packet));
  return SaiDeparser(fields, model);
}

absl::StatusOr<pdpi::BitString> SaiDeparser(const SaiFields& packet,
                                            const z3::model& model) {
  pdpi::BitString result;
  RETURN_IF_ERROR(Deparse(packet.headers.ethernet, model, result));
  RETURN_IF_ERROR(Deparse(packet.headers.ipv4, model, result));
  return result;
}

}  // namespace p4_symbolic

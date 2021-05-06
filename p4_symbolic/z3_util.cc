#include "p4_symbolic/z3_util.h"

#include "absl/status/statusor.h"
#include "gmpxx.h"
#include "gutil/status.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "z3++.h"

namespace p4_symbolic {

z3::context& Z3Context() {
  static z3::context* z3_context = new z3::context();
  return *z3_context;
}

absl::StatusOr<bool> EvalZ3Bool(const z3::expr& bool_expr,
                                const z3::model& model) {
  auto value = model.eval(bool_expr, true).bool_value();
  switch (value) {
    case Z3_L_FALSE:
      return false;
    case Z3_L_TRUE:
      return true;
    default:
      break;
  }
  return gutil::InternalErrorBuilder()
         << "boolean expression '" << bool_expr
         << "' evaluated to unexpected Boolean value " << value;
}

absl::StatusOr<z3::expr> HexStringToZ3Bitvector(const std::string& hex_string,
                                                absl::optional<int> bitwidth) {
  // TODO: Insert check to ensure this won't throw an exception.
  mpz_class integer = mpz_class(hex_string, /*base=*/0);
  std::string decimal = integer.get_str(/*base=*/10);
  if (!bitwidth.has_value()) {
    bitwidth = integer.get_str(/*base=*/2).size();
  }
  return Z3Context().bv_val(decimal.c_str(), *bitwidth);
}

}  // namespace p4_symbolic

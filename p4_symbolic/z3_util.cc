#include "p4_symbolic/z3_util.h"

#include "absl/status/statusor.h"
#include "z3++.h"

namespace p4_symbolic {

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

}  // namespace p4_symbolic

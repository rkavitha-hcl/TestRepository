#ifndef GOOGLE_P4_SYMBOLIC_SAI_TEST_UTIL_H_
#define GOOGLE_P4_SYMBOLIC_SAI_TEST_UTIL_H_

#include <memory>
#include <vector>

#include "absl/status/statusor.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "sai_p4/instantiations/google/instantiations.h"

namespace p4_symbolic {

// Symbolically evaluates the SAI P4 program for the given instantiation with
// the given table entries. If `physical_ports` is non-empty, any solution is
// guaranteed to only use ports from the list.
absl::StatusOr<std::unique_ptr<symbolic::SolverState>> EvaluateSaiPipeline(
    sai::Instantiation instantiation,
    const std::vector<p4::v1::TableEntry>& entries,
    const std::vector<int>& physical_ports = {});

}  // namespace p4_symbolic

#endif  // GOOGLE_P4_SYMBOLIC_SAI_TEST_UTIL_H_

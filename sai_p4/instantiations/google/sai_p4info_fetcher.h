#ifndef GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SAI_P4INFO_FETCHER_H_
#define GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SAI_P4INFO_FETCHER_H_

#include <optional>

#include "absl/types/optional.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/instantiations.h"

namespace sai {

// Enum representing the stage of the CLOS network. Applies to middleblock
// instantiations.
enum class ClosStage {
  kStage2,
  kStage3,
};

// FetchP4Info is used for advanced fetching of static P4Info files.
// Specifically, this function allows fetching of specialized versions of the
// P4Info for stages of an instance. Today, this only includes ECMP annotation
// differences. In most cases, prefer to use the functions in sai_p4info.h.
//
// If the provided ClosStage is not applicable to the current instantiation, the
// function will ignore the stage and return a default P4Info for the
// instantiation (the same behavior as if the stage was not provided at all).
p4::config::v1::P4Info FetchP4Info(
    Instantiation instantiation,
    absl::optional<ClosStage> stage = absl::optional<ClosStage>());

p4::config::v1::P4Info FetchUnionedP4Info();

}  // namespace sai

#endif  // GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SAI_P4INFO_FETCHER_H_

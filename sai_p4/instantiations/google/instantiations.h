#ifndef GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_GOOGLE_INSTANTIATIONS_H_
#define GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_GOOGLE_INSTANTIATIONS_H_

#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"

namespace sai {

// Describes the role of a switch.
// Switches in the same role use the same P4 program (though the P4Info may be
// slightly modified further for each switch within a role, e.g. to configure
// the hashing seed).
enum class Instantiation {
  kMiddleblock,
  kWbb,
};

// Returns all switch roles.
inline std::vector<Instantiation> AllInstantiations() {
  return {Instantiation::kMiddleblock, Instantiation::kWbb};
}

// Returns the name of the given switch role.
inline std::string InstantiationToString(Instantiation role) {
  switch (role) {
    case Instantiation::kMiddleblock:
      return "middleblock";
    case Instantiation::kWbb:
      return "wbb";
  }
  LOG(DFATAL) << "invalid Instantiation: " << static_cast<int>(role);
  return "";
}

// Returns the name of the given switch role.
inline absl::StatusOr<Instantiation> StringToInstantiation(
    const std::string& instantiation) {
  if (instantiation == InstantiationToString(Instantiation::kMiddleblock)) {
    return Instantiation::kMiddleblock;
  } else if (instantiation == InstantiationToString(Instantiation::kWbb)) {
    return Instantiation::kWbb;
  }
  return absl::InvalidArgumentError(
      absl::StrCat("invalid Instantiation: ", instantiation));
}

}  // namespace sai

#endif  // GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_GOOGLE_INSTANTIATIONS_H_

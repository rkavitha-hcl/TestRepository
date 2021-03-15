#ifndef GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SWITCH_ROLE_H_
#define GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SWITCH_ROLE_H_

#include <vector>

#include "glog/logging.h"

namespace sai {

// Describes the role of a switch.
// Switches in the same role use the same P4 program (though the P4Info may be
// slightly modified further for each switch within a role, e.g. to configure
// the hashing seed).
enum class SwitchRole {
  kMiddleblock,
  kWbb,
};

// Returns all switch roles.
inline std::vector<SwitchRole> AllSwitchRoles() {
  return {SwitchRole::kMiddleblock, SwitchRole::kWbb};
}

// Returns the name of the given switch role.
inline std::string SwitchRoleToString(SwitchRole role) {
  switch (role) {
    case SwitchRole::kMiddleblock:
      return "middleblock";
    case SwitchRole::kWbb:
      return "wbb";
  }
  LOG(DFATAL) << "invalid SwitchRole: " << static_cast<int>(role);
  return "";
}

}  // namespace sai

#endif  // GOOGLE_SAI_P4_INSTANTIATIONS_GOOGLE_SWITCH_ROLE_H_

#ifndef GOOGLE_TESTS_LIB_SWITCH_TEST_SETUP_HELPERS_H_
#define GOOGLE_TESTS_LIB_SWITCH_TEST_SETUP_HELPERS_H_

#include <memory>
#include <string>
#include <utility>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "thinkit/switch.h"
namespace pins_test {

// Configures the switch and sets up a P4 Runtime Session. If you don't have
// particular requirements, this is likely the function you want to use.
// Specifically:
// * Creates a session.
// * Clears all tables.
// * Pushes the given `gnmi_config` and waits for the switch to converge.
// * Pushes the given P4Info via RECONCILE_AND_COMMIT.
absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>>
ConfigureSwitchAndReturnP4RuntimeSession(
    thinkit::Switch& thinkit_switch, const std::string& gnmi_config,
    const p4::config::v1::P4Info& p4info,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata = {});

// Configures the switch and sets up a P4 Runtime Session without pushing a
// P4Info. Specifically:
// * Creates a session.
// * Clears all tables.
// * Pushes the given `gnmi_config` and waits for the switch to converge.
// NOTE: Prefer ConfigureSwitchAndReturnP4RuntimeSession over this function
// unless you have special requirements for your P4Info push.
absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>>
ConfigureSwitchAndReturnP4RuntimeSessionWithoutP4InfoPush(
    thinkit::Switch& thinkit_switch, const std::string& gnmi_config,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata = {});

// Configures a pair of switches and sets up P4 Runtime Sessions. If you are
// setting up a pair of switches (e.g. in a mirror testbed) with the same gNMI
// config and P4Info and want to wait for the gNMI configs to converge in
// parallel, this is likely the function that you should use. Specifically, it:
// * Creates two sessions.
// * Clears all tables.
// * Pushes the given `gnmi_config` and waits for the switches to converge.
// * Pushes the given P4Info via RECONCILE_AND_COMMIT.
absl::StatusOr<std::pair<std::unique_ptr<pdpi::P4RuntimeSession>,
                         std::unique_ptr<pdpi::P4RuntimeSession>>>
ConfigureSwitchPairAndReturnP4RuntimeSessionPair(
    thinkit::Switch& thinkit_switch1, thinkit::Switch& thinkit_switch2,
    const std::string& gnmi_config, const p4::config::v1::P4Info& p4info,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata = {});

}  // namespace pins_test

#endif  // GOOGLE_TESTS_LIB_SWITCH_TEST_SETUP_HELPERS_H_

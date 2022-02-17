#include "tests/lib/switch_test_setup_helpers.h"

#include <future>  // NOLINT: third_party library.
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include "glog/logging.h"
#include "gutil/proto.h"
#include "gutil/status.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "tests/thinkit_sanity_tests.h"

namespace pins_test {
namespace {

constexpr absl::Duration kGnmiTimeoutDefault = absl::Minutes(3);

absl::Status ClearTableEntries(
    thinkit::Switch& thinkit_switch,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata) {
  ASSIGN_OR_RETURN(std::unique_ptr<pdpi::P4RuntimeSession> session,
                   pdpi::P4RuntimeSession::Create(thinkit_switch, metadata));
  RETURN_IF_ERROR(pdpi::ClearTableEntries(session.get()));
  RETURN_IF_ERROR(session->Finish());
  return absl::OkStatus();
}

absl::Status PushGnmiAndWaitForConvergence(thinkit::Switch& thinkit_switch,
                                           const std::string& gnmi_config,
                                           absl::Duration gnmi_timeout) {
  RETURN_IF_ERROR(PushGnmiConfig(thinkit_switch, gnmi_config));
  return WaitForGnmiPortIdConvergence(thinkit_switch, gnmi_config,
                                      gnmi_timeout);
}

absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>>
CreateP4RuntimeSessionAndOptionallyPushP4Info(
    thinkit::Switch& thinkit_switch,
    std::optional<p4::config::v1::P4Info> p4info,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata) {
  ASSIGN_OR_RETURN(std::unique_ptr<pdpi::P4RuntimeSession> session,
                   pdpi::P4RuntimeSession::Create(thinkit_switch, metadata));

  if (p4info.has_value()) {
    // Check if P4Info already exists, and if so reboot to workaround PINS
    // limitations (b/200209778).
    ASSIGN_OR_RETURN(p4::v1::GetForwardingPipelineConfigResponse response,
                     pdpi::GetForwardingPipelineConfig(session.get()));
    ASSIGN_OR_RETURN(std::string p4info_diff,
                     gutil::ProtoDiff(*p4info, response.config().p4info()));
    if (response.config().has_p4info() && !p4info_diff.empty()) {
      LOG(WARNING)
          << "Rebooting since P4Info reconfiguration is unsupported by PINS, "
             "but I am asked to push a P4Info with the following diff:\n"
          << p4info_diff;
      RETURN_IF_ERROR(session->Finish());
      TestGnoiSystemColdReboot(thinkit_switch);
      // Reconnect after reboot.
      ASSIGN_OR_RETURN(
          session, pdpi::P4RuntimeSession::Create(thinkit_switch, metadata));
    }

    // Push P4Info.
    RETURN_IF_ERROR(pdpi::SetForwardingPipelineConfig(
        session.get(),
        p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
        *p4info));
  }

  RETURN_IF_ERROR(pdpi::CheckNoTableEntries(session.get()));
  return session;
}

}  // namespace

absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>>
ConfigureSwitchAndReturnP4RuntimeSession(
    thinkit::Switch& thinkit_switch, std::optional<std::string> gnmi_config,
    std::optional<p4::config::v1::P4Info> p4info,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata) {
  // Since the gNMI Config push relies on tables being cleared, we construct a
  // P4RuntimeSession and clear the tables first.
  RETURN_IF_ERROR(ClearTableEntries(thinkit_switch, metadata));

  if (gnmi_config.has_value()) {
    RETURN_IF_ERROR(
        PushGnmiAndWaitForConvergence(thinkit_switch, *gnmi_config,
                                      /*gnmi_timeout=*/kGnmiTimeoutDefault));
  }

  return CreateP4RuntimeSessionAndOptionallyPushP4Info(thinkit_switch, p4info,
                                                       metadata);
}

absl::StatusOr<std::pair<std::unique_ptr<pdpi::P4RuntimeSession>,
                         std::unique_ptr<pdpi::P4RuntimeSession>>>
ConfigureSwitchPairAndReturnP4RuntimeSessionPair(
    thinkit::Switch& thinkit_switch1, thinkit::Switch& thinkit_switch2,
    std::optional<std::string> gnmi_config,
    std::optional<p4::config::v1::P4Info> p4info,
    const pdpi::P4RuntimeSessionOptionalArgs& metadata) {
  // We configure both switches in parallel, since it may require rebooting the
  // switch which is costly.
  using T = absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>>;
  T session1, session2;
  {
    std::future<T> future1 = std::async(std::launch::async, [&] {
      return ConfigureSwitchAndReturnP4RuntimeSession(
          thinkit_switch1, gnmi_config, p4info, metadata);
    });
    std::future<T> future2 = std::async(std::launch::async, [&] {
      return ConfigureSwitchAndReturnP4RuntimeSession(
          thinkit_switch2, gnmi_config, p4info, metadata);
    });
    session1 = future1.get();
    session2 = future2.get();
  }
  RETURN_IF_ERROR(session1.status()).SetPrepend()
      << "failed to configure switch '" << thinkit_switch1.ChassisName()
      << "': ";
  RETURN_IF_ERROR(session2.status()).SetPrepend()
      << "failed to configure switch '" << thinkit_switch2.ChassisName()
      << "': ";
  return std::make_pair(std::move(*session1), std::move(*session2));
}

}  // namespace pins_test

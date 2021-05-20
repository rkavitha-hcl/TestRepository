#ifndef GOOGLE_TESTS_FORWARDING_MASTER_ARBITRATION_TEST_H_
#define GOOGLE_TESTS_FORWARDING_MASTER_ARBITRATION_TEST_H_

#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "google/rpc/code.pb.h"
#include "grpcpp/grpcpp.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "tests/forwarding/p4_blackbox_fixture.h"
#include "thinkit/test_environment.h"

namespace gpins {

using ::p4::v1::P4Runtime;

class MasterArbitrationTestFixture : public thinkit::MirrorTestbedFixture {
 public:
  void SetUp() {
    MirrorTestbedFixture::SetUp();
    // Push the gnmi configuration.
    ASSERT_OK(
        pins_test::PushGnmiConfig(GetMirrorTestbed().Sut(), GetGnmiConfig()));
    ASSERT_OK(pins_test::PushGnmiConfig(GetMirrorTestbed().ControlSwitch(),
                                        GetGnmiConfig()));

    device_id_ = GetMirrorTestbed().Sut().DeviceId();
    //  Sleep for one second, so that we are guaranteed to get a higher
    //  election id than the previous test (we use unix seconds in production
    //  for the upper election id bits, too).
    absl::SleepFor(absl::Seconds(1));
    upper_election_id_ = absl::ToUnixSeconds(absl::Now());
  }

  void ClearSwitchState(pdpi::P4RuntimeSession* p4rt_session) {
    // Clear entries here in case the previous test did not (e.g. because it
    // crashed).
    ASSERT_OK(pdpi::ClearTableEntries(
        p4rt_session, sai::GetIrP4Info(sai::Instantiation::kMiddleblock)));
    // Check that switch is in a clean state.
    ASSERT_OK_AND_ASSIGN(auto read_back_entries,
                         pdpi::ReadPiTableEntries(p4rt_session));
    ASSERT_EQ(read_back_entries.size(), 0);
  }

  // Returns a P4Runtime stub.
  absl::StatusOr<std::unique_ptr<P4Runtime::Stub>> Stub() {
    return GetMirrorTestbed().Sut().CreateP4RuntimeStub();
  }

  // Makes an election ID given the lower bits. The upper bits are fixed to
  // roughly the current time in seconds, such that we are guaranteed to always
  // get monotonically increasing IDs.
  absl::uint128 ElectionIdFromLower(uint64_t lower_election_id) const {
    return absl::MakeUint128(upper_election_id_, lower_election_id);
  }

  // Attempts to become master on a given stub.
  // TODO Consider replace slave/master. See go/cstyle#Inclusive_Language
  // - "In all code, including naming and comments, use inclusive language and
  // avoid terms that other programmers might find disrespectful or offensive
  // (such as "master" and "slave", "blacklist" and "whitelist", or "redline"),
  // even if the terms also have an ostensibly neutral meaning.".
  absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>> BecomeMaster(
      std::unique_ptr<P4Runtime::Stub> stub, uint64_t lower_election_id) const {
    return pdpi::P4RuntimeSession::Create(
        std::move(stub), device_id_,
        pdpi::P4RuntimeSessionOptionalArgs{
            .election_id = ElectionIdFromLower(lower_election_id)});
  }

  // Attempts to become master on a new stub.
  absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>> BecomeMaster(
      uint64_t lower_election_id) {
    ASSIGN_OR_RETURN(auto stub, Stub());
    return BecomeMaster(std::move(stub), lower_election_id);
  }

  uint32_t DeviceId() const { return device_id_; }

  thinkit::TestEnvironment& TestEnvironment() {
    return GetMirrorTestbed().Environment();
  }

  const pdpi::IrP4Info& IrP4Info() const { return ir_p4info_; }

 private:
  uint64_t upper_election_id_;
  uint32_t device_id_;
  pdpi::IrP4Info ir_p4info_ =
      sai::GetIrP4Info(sai::Instantiation::kMiddleblock);
};

}  // namespace gpins

#endif  // GOOGLE_TESTS_FORWARDING_MASTER_ARBITRATION_TEST_H_

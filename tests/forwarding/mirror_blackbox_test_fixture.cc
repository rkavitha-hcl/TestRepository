#include "tests/forwarding/mirror_blackbox_test_fixture.h"

#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "tests/lib/switch_test_setup_helpers.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace pins_test {

void MirrorBlackboxTestFixture::SetUp() {
  thinkit::MirrorTestbedFixture::SetUp();

  // Initialize the connection, clear table entries, and push GNMI
  // configuration for the SUT and Control switch.
  ASSERT_OK_AND_ASSIGN(
      std::tie(sut_p4rt_session_, control_switch_p4rt_session_),
      pins_test::ConfigureSwitchPairAndReturnP4RuntimeSessionPair(
          GetMirrorTestbed().Sut(), GetMirrorTestbed().ControlSwitch(),
          GetGnmiConfig(), GetP4Info()));
}

void MirrorBlackboxTestFixture::TearDown() {
  // Clear all table entries to leave the sut and control switch in a clean
  // state.
  EXPECT_OK(pdpi::ClearTableEntries(&GetSutP4RuntimeSession()));
  EXPECT_OK(pdpi::ClearTableEntries(&GetControlP4RuntimeSession()));

  MirrorTestbedFixture::TearDown();
}

}  // namespace pins_test

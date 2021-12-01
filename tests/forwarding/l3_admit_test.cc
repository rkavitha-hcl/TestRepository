// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "tests/forwarding/l3_admit_test.h"

#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gutil/status_matchers.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace gpins {
void L3AdmitTestFixture::SetUp() {
  thinkit::MirrorTestbedFixture::SetUp();

  // Configure the SUT switch.
  ASSERT_OK(
      pins_test::PushGnmiConfig(GetMirrorTestbed().Sut(), GetGnmiConfig()));
  ASSERT_OK_AND_ASSIGN(p4rt_sut_switch_session_,
                       pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
                           GetMirrorTestbed().Sut(), GetP4Info()));

  // Configure the control switch.
  ASSERT_OK(pins_test::PushGnmiConfig(GetMirrorTestbed().ControlSwitch(),
                                      GetGnmiConfig()));
  ASSERT_OK_AND_ASSIGN(p4rt_control_switch_session_,
                       pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
                           GetMirrorTestbed().ControlSwitch(), GetP4Info()));
}

void L3AdmitTestFixture::TearDown() {
  thinkit::MirrorTestbedFixture::TearDown();
}

TEST_P(L3AdmitTestFixture, L3PacketsAreRoutedWhenMacAddressIsInMyStation) {
  LOG(INFO) << "Starting test.";
  LOG(INFO) << GetP4Info().DebugString();
}

}  // namespace gpins

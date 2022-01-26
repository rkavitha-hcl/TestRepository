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

#include "tests/forwarding/p4info_push_test.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"

// Note: "gutil/status_matchers.h" is needed for GitHub builds to succeed.

namespace gpins {
namespace {

// Sends P4Info to the switch and makes sure it works.
TEST_P(P4InfoPushTestFixture, P4InfoPushTest) {
  LOG(INFO) << "Test started";
  thinkit::Switch& sut = GetTestbed().Sut();
  const p4::config::v1::P4Info& p4info = GetParam().p4info;

  // Push the gNMI configuration to the SUT switch.
  LOG(INFO) << "Pushing gNMI config";
  ASSERT_OK(pins_test::PushGnmiConfig(sut, GetParam().gnmi_config));
  // Not pushing the control switch's gNMI config as we do not use that switch
  // in this test.

  // Initialize the P4RT session.
  LOG(INFO) << "Establishing P4RT session";
  ASSERT_OK_AND_ASSIGN(auto sut_p4rt_session,
                       pdpi::P4RuntimeSession::Create(sut));

  // TODO: currently have to reboot the switch if P4Info is already
  // present, as it doesn't support pushing different P4Infos without a restart.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::GetForwardingPipelineConfigResponse p4_config,
      pdpi::GetForwardingPipelineConfig(sut_p4rt_session.get()));
  if (p4_config.config().has_p4info()) {
    RebootSut();
    // Reconnect after reboot.
    ASSERT_OK_AND_ASSIGN(sut_p4rt_session, pdpi::P4RuntimeSession::Create(sut));
  }

  // Push P4Info.
  LOG(INFO) << "Pushing P4Info";
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      sut_p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      p4info));

  // Pull P4Info, make sure it is the same as the pushed one.
  LOG(INFO) << "Pulling P4Info";
  ASSERT_OK_AND_ASSIGN(const auto response,
                       pdpi::GetForwardingPipelineConfig(
                           sut_p4rt_session.get(),
                           p4::v1::GetForwardingPipelineConfigRequest::ALL));
  ASSERT_THAT(response.config().p4info(), gutil::EqualsProto(p4info));

  LOG(INFO) << "Test finished successfully";
}

// TODO: implement a negative test.
}  // namespace
}  // namespace gpins

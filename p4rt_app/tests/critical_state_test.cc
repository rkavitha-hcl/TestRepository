// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "absl/status/status.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/ir.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::HasSubstr;

class CriticalStateTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    LOG(INFO) << "Opening P4RT connection to " << address << ".";
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));

    // Push a P4Info file to enable the reading, and writing of entries.
    ASSERT_OK(pdpi::SetForwardingPipelineConfig(
        p4rt_session_.get(),
        p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
        p4_info_));
  }

  const p4::config::v1::P4Info p4_info_ =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);
  const pdpi::IrP4Info ir_p4_info_ =
      sai::GetIrP4Info(sai::Instantiation::kMiddleblock);

  test_lib::P4RuntimeGrpcService p4rt_service_ =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(CriticalStateTest, PipelineConfgIsRejectedWhenCritical) {
  p4rt_service_.GetComponentStateHelper().ReportComponentState(
      swss::ComponentState::kInactive, "some reason");
  EXPECT_THAT(
      pdpi::SetForwardingPipelineConfig(
          p4rt_session_.get(),
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4_info_),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("some reason")));
}

TEST_F(CriticalStateTest, WriteRequestIsRejectedWhenCritical) {
  // Verify we can send a write while NOT in a critical state.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv4_table_entry {
                  match {
                    vrf_id: "50"
                    ipv4_dst { value: "10.81.8.0" prefix_length: 23 }
                  }
                  action { set_nexthop_id { nexthop_id: "8" } }
                }
              }
            }
          )pb",
          ir_p4_info_));
  ASSERT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Set the switch into critical state.
  p4rt_service_.GetComponentStateHelper().ReportComponentState(
      swss::ComponentState::kInactive, "some reason");

  // Try writing again. We should fail with an INTERNAL error.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("some reason")));
}

TEST_F(CriticalStateTest, CanReadWhileCritical) {
  // Write an entry which we can read back out.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv4_table_entry {
                  match {
                    vrf_id: "50"
                    ipv4_dst { value: "10.81.8.0" prefix_length: 23 }
                  }
                  action { set_nexthop_id { nexthop_id: "8" } }
                }
              }
            }
          )pb",
          ir_p4_info_));
  ASSERT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Set the switch into critical state.
  p4rt_service_.GetComponentStateHelper().ReportComponentState(
      swss::ComponentState::kInactive, "some reason");

  // Read should still work and return the table entry.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  EXPECT_EQ(read_response.entities_size(), 1);
}

// INTERNAL errors are difficult to come by since the P4RT app is designed such
// that any input is handled cleanly. One hole we cannot fill relates to
// deletion of VRFs. Delete's for VRFs come after all flows are removed to avoid
// race conditions. A failure to delete a VRF means we would need to restore all
// flows using that VRF. This analysis and implementation would be tricky (read
// difficult) to get right, and because this is an unlikely case we simply put
// the switch into a critical state.
TEST_F(CriticalStateTest, FailureToDeleteAVrfCausesCritical) {
  // Install an IP flow that uses a VRF.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv4_table_entry {
                  match {
                    vrf_id: "50"
                    ipv4_dst { value: "10.81.8.0" prefix_length: 23 }
                  }
                  action { set_nexthop_id { nexthop_id: "8" } }
                }
              }
            }
          )pb",
          ir_p4_info_));
  ASSERT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Setup the response path to fail on the VRF delete. Which should result in a
  // critical state.
  p4rt_service_.GetVrfAppDbTable().SetResponseForKey(
      "p4rt-50", "SWSS_RC_INVALID_PARAM", "my error message");
  request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kInternal));

  // Since we're critical we should not be allowed to send any new write
  // requests to the P4RT App.
  p4rt_service_.GetVrfAppDbTable().SetResponseForKey("p4rt-50",
                                                     "SWSS_RC_SUCCESS", "");
  request.mutable_updates(0)->set_type(p4::v1::Update::MODIFY);
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace p4rt_app

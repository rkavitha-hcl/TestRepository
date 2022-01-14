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
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/repeated_field.h"
#include "grpcpp/client_context.h"
#include "grpcpp/security/credentials.h"
#include "grpcpp/support/status.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::p4::v1::GetForwardingPipelineConfigResponse;
using ::p4::v1::SetForwardingPipelineConfigRequest;
using ::p4::v1::SetForwardingPipelineConfigResponse;
using ::testing::IsEmpty;
using ::testing::Not;

MATCHER_P(GrpcStatusIs, status_code, "") {
  return arg.error_code() == status_code;
}

class ForwardingPipelineConfigTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    LOG(INFO) << "Opening P4RT connection to " << address << ".";
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));
  }

  // SetForwardingPipelineConfig will reject any flow that doesn't have an
  // expected 'device ID', 'role', or 'election ID'. Since this information is
  // irrelevant to these test we use a helper function to simplify setup.
  SetForwardingPipelineConfigRequest GetBasicForwardingRequest() {
    SetForwardingPipelineConfigRequest request;
    request.set_device_id(p4rt_session_->DeviceId());
    request.set_role(p4rt_session_->Role());
    *request.mutable_election_id() = p4rt_session_->ElectionId();
    return request;
  }

  test_lib::P4RuntimeGrpcService p4rt_service_ =
      test_lib::P4RuntimeGrpcService(P4RuntimeImplOptions{});
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(ForwardingPipelineConfigTest, VerifyWillNotUpdateAppDbState) {
  // By using the "middleblock" config we expect the ACL table definitionss to
  // be written into the AppDb during a config push.
  auto request = GetBasicForwardingRequest();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY);
  *request.mutable_config()->mutable_p4info() =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);

  // However, since we're only verifying the config we should not see anything
  // being written to the AppDb tables.
  SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_OK(p4rt_session_->Stub().SetForwardingPipelineConfig(&context, request,
                                                              &response));
  EXPECT_THAT(p4rt_service_.GetP4rtAppDbTable().GetAllKeys(), IsEmpty());
}

TEST_F(ForwardingPipelineConfigTest, VerifyFailsWhenNoConfigIsSet) {
  auto request = GetBasicForwardingRequest();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY);

  SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_THAT(p4rt_session_->Stub().SetForwardingPipelineConfig(
                  &context, request, &response),
              GrpcStatusIs(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST_F(ForwardingPipelineConfigTest, VerifyAndCommitWillUpdateAppDbState) {
  // By using the "middleblock" config we expect the ACL table definitionss to
  // be written into the AppDb during a config push.
  auto request = GetBasicForwardingRequest();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT);
  *request.mutable_config()->mutable_p4info() =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);

  // Since we're both verifying and committing the config we expect to see
  // changes to the AppDb tables.
  SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_OK(p4rt_session_->Stub().SetForwardingPipelineConfig(&context, request,
                                                              &response));
  EXPECT_THAT(p4rt_service_.GetP4rtAppDbTable().GetAllKeys(), Not(IsEmpty()));
}

TEST_F(ForwardingPipelineConfigTest, VerifyAndCommitFailsWhenNoConfigIsSet) {
  auto request = GetBasicForwardingRequest();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT);

  SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_THAT(p4rt_session_->Stub().SetForwardingPipelineConfig(
                  &context, request, &response),
              GrpcStatusIs(grpc::StatusCode::INVALID_ARGUMENT));
}

TEST_F(ForwardingPipelineConfigTest,
       VerifyAndCommitCannotClearForwardingState) {
  auto request = GetBasicForwardingRequest();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY_AND_COMMIT);
  *request.mutable_config()->mutable_p4info() =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);

  // For the first config push we expect everything to pass since the switch is
  // in a clean state.
  {
    SetForwardingPipelineConfigResponse response;
    grpc::ClientContext context;
    ASSERT_OK(p4rt_session_->Stub().SetForwardingPipelineConfig(
        &context, request, &response));
  }

  // This is not expected P4Runtime behavior. We simply haven't implemented it
  // today, and currently have no plans to.
  {
    SetForwardingPipelineConfigResponse response;
    grpc::ClientContext context;
    EXPECT_THAT(p4rt_session_->Stub().SetForwardingPipelineConfig(
                    &context, request, &response),
                GrpcStatusIs(grpc::StatusCode::UNIMPLEMENTED));
  }
}

TEST_F(ForwardingPipelineConfigTest, SetForwardingPipelineConfig) {
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(sai::Instantiation::kMiddleblock)));
}

TEST_F(ForwardingPipelineConfigTest, GetForwardingPipelineConfig) {
  const p4::config::v1::P4Info p4_info =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4_info));
  ASSERT_OK_AND_ASSIGN(GetForwardingPipelineConfigResponse response,
                       pdpi::GetForwardingPipelineConfig(
                           p4rt_session_.get(),
                           p4::v1::GetForwardingPipelineConfigRequest::ALL));

  // Ensure the P4Info we read back matches what we set.
  EXPECT_THAT(response.config().p4info(), gutil::EqualsProto(p4_info));
}

TEST_F(ForwardingPipelineConfigTest, SetDuplicateForwardingPipelineConfig) {
  auto p4_info = sai::GetP4Info(sai::Instantiation::kMiddleblock);
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4_info));
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4_info));
}

TEST_F(ForwardingPipelineConfigTest, FailVerifyAndSave) {
  pdpi::P4RuntimeSession* session = p4rt_session_.get();
  SetForwardingPipelineConfigRequest request;
  request.set_device_id(session->DeviceId());
  request.set_role(session->Role());
  *request.mutable_election_id() = session->ElectionId();
  request.set_action(SetForwardingPipelineConfigRequest::VERIFY_AND_SAVE);

  SetForwardingPipelineConfigResponse response;
  grpc::ClientContext context;
  EXPECT_THAT(
      gutil::GrpcStatusToAbslStatus(session->Stub().SetForwardingPipelineConfig(
          &context, request, &response)),
      gutil::StatusIs(absl::StatusCode::kUnimplemented));
}

TEST_F(ForwardingPipelineConfigTest, ModifyConfig) {
  auto p4_info = sai::GetP4Info(sai::Instantiation::kMiddleblock);
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4_info));
  p4_info.mutable_tables()->RemoveLast();
  EXPECT_THAT(
      pdpi::SetForwardingPipelineConfig(
          p4rt_session_.get(),
          SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT, p4_info),
      gutil::StatusIs(absl::StatusCode::kUnimplemented,
                      testing::HasSubstr("deleted: ")));
}

TEST_F(ForwardingPipelineConfigTest,
       RejectWriteRequestsIfForwardingPipelineConfigFails) {
  auto p4_info = sai::GetP4Info(sai::Instantiation::kMiddleblock);
  auto ir_p4_info = sai::GetIrP4Info(sai::Instantiation::kMiddleblock);

  // Generate error from the OrchAgent layer when programming the PRE_INGRESS
  // ACL table.
  p4rt_service_.GetP4rtAppDbTable().SetResponseForKey(
      "DEFINITION:ACL_ACL_PRE_INGRESS_TABLE", "SWSS_RC_INVALID_PARAM",
      "my error message");
  ASSERT_THAT(pdpi::SetForwardingPipelineConfig(
                  p4rt_session_.get(),
                  SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
                  sai::GetP4Info(sai::Instantiation::kMiddleblock)),
              StatusIs(absl::StatusCode::kInternal));

  // Because we failed to program the forwarding pipeline config we should not
  // be able to write to the table.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_pre_ingress_table_entry {
                                   match {}
                                   priority: 2000
                                   action { set_vrf { vrf_id: "20" } }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace p4rt_app

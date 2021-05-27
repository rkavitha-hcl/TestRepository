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
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::AllOf;
using ::testing::HasSubstr;

class ResponsePathTest : public testing::Test {
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
        sai::GetP4Info(sai::Instantiation::kMiddleblock)));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(ResponsePathTest, InsertRequestFails) {
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554496
                 match {
                   field_id: 1
                   exact { value: "1" }
                 }
                 match {
                   field_id: 2
                   exact { value: "fe80::021a:11ff:fe17:5f80" }
                 }
                 action {
                   action {
                     action_id: 16777217
                     params { param_id: 1 value: "\000\032\021\027_\200" }
                   }
                 }
               }
             }
           })pb",
      &request));

  auto neighbor_entry =
      test_lib::AppDbEntryBuilder{}
          .SetTableName("FIXED_NEIGHBOR_TABLE")
          .AddMatchField("neighbor_id", "fe80::021a:11ff:fe17:5f80")
          .AddMatchField("router_interface_id", "1");

  // Assume the Orchagent fails with an invalid parameter.
  p4rt_service_.GetP4rtAppDbTable().SetResponseForKey(
      neighbor_entry.GetKey(), "SWSS_RC_INVALID_PARAM", "my error message");

  // We expect the invalid argument error to be propagated all the way back to
  // the gRPC response.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("#1: INVALID_ARGUMENT: my error message")));
}

TEST_F(ResponsePathTest, OneOfManyInsertRequestFails) {
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554496
                 match {
                   field_id: 1
                   exact { value: "1" }
                 }
                 match {
                   field_id: 2
                   exact { value: "1" }
                 }
                 action {
                   action {
                     action_id: 16777217
                     params { param_id: 1 value: "\000\032\021\027_\200" }
                   }
                 }
               }
             }
           }
           updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554498
                 match {
                   field_id: 1
                   exact { value: "8" }
                 }
                 action {
                   action {
                     action_id: 16777219
                     params { param_id: 1 value: "8" }
                     params { param_id: 2 value: "1" }
                   }
                 }
               }
             }
           })pb",
      &request));

  auto nexthop_entry = test_lib::AppDbEntryBuilder{}
                           .SetTableName("FIXED_NEXTHOP_TABLE")
                           .AddMatchField("nexthop_id", "8");

  // Assume the Orchagent fails for one request with an invalid parameter.
  p4rt_service_.GetP4rtAppDbTable().SetResponseForKey(
      nexthop_entry.GetKey(), "SWSS_RC_INVALID_PARAM", "my error message");

  // When one of the requests fails, but the other succeeds we expect the
  // response to tell us the status both separately.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown,
               AllOf(HasSubstr("#1: OK"),
                     HasSubstr("#2: INVALID_ARGUMENT: my error message"))));
}

TEST_F(ResponsePathTest, RequestWithDuplicateKeysFails) {
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554496
                 match {
                   field_id: 1
                   exact { value: "1" }
                 }
                 match {
                   field_id: 2
                   exact { value: "1" }
                 }
                 action {
                   action {
                     action_id: 16777217
                     params { param_id: 1 value: "\000\032\021\027_\200" }
                   }
                 }
               }
             }
           }
           updates {
             type: MODIFY
             entity {
               table_entry {
                 table_id: 33554496
                 match {
                   field_id: 1
                   exact { value: "1" }
                 }
                 match {
                   field_id: 2
                   exact { value: "1" }
                 }
                 action {
                   action {
                     action_id: 16777217
                     params { param_id: 1 value: "\000\032\021\027_\200" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // We expect the invalid argument error to be propagated all the way back to
  // the gRPC response.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown,
               AllOf(HasSubstr("#1: INVALID_ARGUMENT:"),
                     HasSubstr("#2: INVALID_ARGUMENT:"))));
}

}  // namespace
}  // namespace p4rt_app

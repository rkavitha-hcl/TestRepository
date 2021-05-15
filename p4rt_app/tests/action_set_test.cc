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
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::EqualsProto;

class ActionSetTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    LOG(INFO) << "Opening P4RT connection to " << address << ".";
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));
    // Configure ethernet ports before the P4Info push.
    p4rt_service_.GetPortAppDbTable().InsertTableEntry("Ethernet0",
                                                       {{"id", "1"}});

    // Push a P4Info file to enable the reading, and writing of entries.
    ASSERT_OK(pdpi::SetForwardingPipelineConfig(
        p4rt_session_.get(),
        p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
        sai::GetP4Info(sai::Instantiation::kMiddleblock)));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(ActionSetTest, WcmpInsertReadAndRemove) {
  // Set the WCMP write request that has 2 actions with different weights.
  p4::v1::WriteRequest write_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554499
                 match {
                   field_id: 1
                   exact { value: "8" }
                 }
                 action {
                   action_profile_action_set {
                     action_profile_actions {
                       action {
                         action_id: 16777221
                         params { param_id: 1 value: "80" }
                       }
                       weight: 1
                       watch_port: "1"
                     }
                     action_profile_actions {
                       action {
                         action_id: 16777221
                         params { param_id: 1 value: "20" }
                       }
                       weight: 2
                     }
                   }
                 }
               }
             }
           })pb",
      &write_request));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));

  // Reading back the flows should result in the same table entry.
  p4::v1::ReadRequest read_request;
  auto* entity = read_request.add_entities();
  entity->mutable_table_entry()->set_table_id(0);
  entity->mutable_table_entry()->set_priority(0);
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(write_request.updates(0).entity()));

  // Modify the P4 write request to delete the entry which should not fail since
  // we know it does exist.
  write_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));

  // Reading back the entry should result in nothing being returned.
  ASSERT_OK_AND_ASSIGN(read_response, pdpi::SetMetadataAndSendPiReadRequest(
                                          p4rt_session_.get(), read_request));
  EXPECT_EQ(read_response.entities_size(), 0);
}

}  // namespace
}  // namespace p4rt_app

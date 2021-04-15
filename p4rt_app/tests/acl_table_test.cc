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

using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::UnorderedElementsAreArray;

class AclTableTest : public testing::Test {
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
        p4rt_session_.get(), sai::GetP4Info(sai::Instantiation::kMiddleblock)));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(AclTableTest, SetVrfFlowCreatesVrfTableEntry) {
  // Send the P4 write request to set a VRF ID.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554689
                 priority: 2000
                 action {
                   action {
                     action_id: 16777472
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &request));
  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Verify the correct ACL entry is added the the P4RT table.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("ACL_ACL_LOOKUP_TABLE")
                            .SetPriority(2000)
                            .SetAction("set_vrf")
                            .AddActionParam("vrf_id", "p4rt-20");
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Verify the VRF ID exists.
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"));
}

TEST_F(AclTableTest, VrfTableEntriesPersistsWhileInUse) {
  // Add two ACL flows with different priorities, but use the same VRF ID.
  p4::v1::WriteRequest insert_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554689
                 priority: 2000
                 action {
                   action {
                     action_id: 16777472
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           }
           updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554689
                 priority: 2001
                 action {
                   action {
                     action_id: 16777472
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &insert_request));

  // Insert both flows and verify the VRF ID exists.
  EXPECT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), insert_request));
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"))
      << "VRF ID was never created.";

  // Delete one request, but because the other still uses the VRF ID it should
  // not be removed.
  p4::v1::WriteRequest delete_request;
  *delete_request.add_updates() = insert_request.updates(0);
  delete_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), delete_request));
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"))
      << "VRF ID is still in use and should still exist.";

  // Finally, delete the other request, and verify the VRF ID is also removed.
  delete_request.Clear();
  *delete_request.add_updates() = insert_request.updates(1);
  delete_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), delete_request));
  EXPECT_THAT(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"),
              StatusIs(absl::StatusCode::kNotFound));
}

// TODO: update test to validate meter values.
TEST_F(AclTableTest, ReadMeters) {
  p4::v1::ReadRequest read_request;
  auto* entity = read_request.add_entities();
  entity->mutable_table_entry()->set_table_id(0);
  entity->mutable_table_entry()->set_priority(0);
  entity->mutable_table_entry()->mutable_meter_config();

  EXPECT_OK(pdpi::SetIdAndSendPiReadRequest(p4rt_session_.get(), read_request))
      << "Failing read request: " << read_request.ShortDebugString();
}

}  // namespace
}  // namespace p4rt_app

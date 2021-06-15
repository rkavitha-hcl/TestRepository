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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/pd.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::EqualsProto;
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;
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
        p4rt_session_.get(),
        p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
        p4_info_));
  }

  // AclTableTests are written against the P4 middle block.
  const p4::config::v1::P4Info p4_info_ =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);
  const pdpi::IrP4Info ir_p4_info_ =
      sai::GetIrP4Info(sai::Instantiation::kMiddleblock);

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(AclTableTest, SetVrfFlowCreatesVrfTableEntry) {
  // Send the P4 write request to set a VRF ID.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_lookup_table_entry {
                                   match {}
                                   priority: 2000
                                   action { set_vrf { vrf_id: "20" } }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

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
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest insert_request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_lookup_table_entry {
                                   match {}
                                   priority: 2000
                                   action { set_vrf { vrf_id: "20" } }
                                 }
                               }
                             }
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_lookup_table_entry {
                                   match {}
                                   priority: 2001
                                   action { set_vrf { vrf_id: "20" } }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));

  // Insert both flows and verify the VRF ID exists.
  EXPECT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   insert_request));
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"))
      << "VRF ID was never created.";

  // Delete one request, but because the other still uses the VRF ID it should
  // not be removed.
  p4::v1::WriteRequest delete_request;
  *delete_request.add_updates() = insert_request.updates(0);
  delete_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   delete_request));
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"))
      << "VRF ID is still in use and should still exist.";

  // Finally, delete the other request, and verify the VRF ID is also removed.
  delete_request.Clear();
  *delete_request.add_updates() = insert_request.updates(1);
  delete_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   delete_request));
  EXPECT_THAT(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(AclTableTest, VrfTableEntryDeleteWithWrongValues) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_lookup_table_entry {
                                   match {}
                                   priority: 2000
                                   action { set_vrf { vrf_id: "20" } }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"))
      << "VRF ID was never created.";

  // Delete request using an incorrect action param (vrf 25 instead of 20).
  request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  *request.mutable_updates(0)
       ->mutable_entity()
       ->mutable_table_entry()
       ->mutable_action()
       ->mutable_action()
       ->mutable_params(0)
       ->mutable_value() = "25";
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Expect the correct AppDb entry and its corresponding action param to be
  // cleared since delete only looks at the AppDB key.
  EXPECT_THAT(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"),
              StatusIs(absl::StatusCode::kNotFound));
}

TEST_F(AclTableTest, ReadCounters) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_ingress_table_entry {
                                   match { is_ip { value: "0x1" } }
                                   priority: 10
                                   action { copy { qos_queue: "0x1" } }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Fake OrchAgent updating the counters.
  auto counter_db_entry = test_lib::AppDbEntryBuilder{}
                              .SetTableName("P4RT:ACL_ACL_INGRESS_TABLE")
                              .SetPriority(10)
                              .AddMatchField("is_ip", "0x1");
  p4rt_service_.GetP4rtCountersDbTable().InsertTableEntry(
      counter_db_entry.GetKey(), {{"packets", "1"}, {"bytes", "128"}});

  // Verify the entry we read back has counter information.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));

  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0).table_entry().counter_data(),
              EqualsProto(R"pb(byte_count: 128 packet_count: 1)pb"));
}

// TODO: update test to validate meter values.
TEST_F(AclTableTest, ReadMeters) {
  p4::v1::ReadRequest read_request;
  auto* entity = read_request.add_entities();
  entity->mutable_table_entry()->set_table_id(0);
  entity->mutable_table_entry()->set_priority(0);
  entity->mutable_table_entry()->mutable_meter_config();

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request))
      << "Failing read request: " << read_request.ShortDebugString();
}

TEST_F(AclTableTest, CannotInsertEntryThatFailsAConstraintCheck) {
  // The ACL lookup table requires the is_ipv4 field to be set if we are
  // matching on a dst_ip.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                acl_lookup_table_entry {
                  match { dst_ip { value: "10.0.0.1" mask: "255.255.255.255" } }
                  priority: 2000
                  action { set_vrf { vrf_id: "20" } }
                }
              }
            }
          )pb",
          ir_p4_info_));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

}  // namespace
}  // namespace p4rt_app

// Copyright 2021 Google LLC
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
#include <type_traits>
#include <unordered_map>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/tests/lib/p4runtime_component_test_fixture.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "swss/fakes/fake_sonic_db_table.h"

namespace p4rt_app {
namespace {

using ::gutil::EqualsProto;
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAreArray;

class VrfTableTest : public test_lib::P4RuntimeComponentTestFixture {
 protected:
  VrfTableTest()
      : test_lib::P4RuntimeComponentTestFixture(
            sai::Instantiation::kMiddleblock,
            /*gnmi_ports=*/{}) {}
};

TEST_F(VrfTableTest, SetVrfFlowCreatesVrfTableEntry) {
  // Send the P4 write request to set a VRF ID.
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
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Verify the correct ACL entry is added the the P4RT table.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("ACL_ACL_PRE_INGRESS_TABLE")
                            .SetPriority(2000)
                            .SetAction("set_vrf")
                            .AddActionParam("vrf_id", "p4rt-20");
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Verify the VRF ID exists.
  EXPECT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("p4rt-20"));
}

TEST_F(VrfTableTest, VrfTableEntriesPersistsWhileInUse) {
  // Add two ACL flows with different priorities, but use the same VRF ID.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest insert_request,
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
                             updates {
                               type: INSERT
                               table_entry {
                                 acl_pre_ingress_table_entry {
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

TEST_F(VrfTableTest, VrfTableEntryDeleteWithWrongValues) {
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

// TODO: remove special handling when ONF no longer relies on it.
TEST_F(VrfTableTest, SupportDefaultVrf) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv6_table_entry {
                  match {
                    vrf_id: "vrf-0"
                    ipv6_dst { value: "2002:a17:506:c114::" prefix_length: 64 }
                  }
                  action { set_nexthop_id { nexthop_id: "20" } }
                }
              }
            }
          )pb",
          ir_p4_info_));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV6_TABLE")
                            .AddMatchField("ipv6_dst", "2002:a17:506:c114::/64")
                            .AddMatchField("vrf_id", "")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that the default vrf is translated back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

TEST_F(VrfTableTest, InsertReadAndDeleteEntry) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));

  // Create the VRF entry, and do a sanity check that it exists in the
  // VRF_TABLE.
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  ASSERT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("vrf-0"))
      << "VRF ID was never created.";

  // Read back the VRF entry which should result in the same table entry.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));

  // Delete the VRF entry, and do a sanity check that it is gone.
  request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  ASSERT_THAT(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("vrf-0"),
              StatusIs(absl::StatusCode::kNotFound))
      << "VRF ID was not deleted.";
}

TEST_F(VrfTableTest, CannotModifyEntries) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: MODIFY
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

TEST_F(VrfTableTest, CannotInsertDuplicateEntries) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: ALREADY_EXISTS")));
}

TEST_F(VrfTableTest, InsertRequestFails) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));

  // Assume the Orchagent fails with an invalid parameter.
  p4rt_service_.GetVrfAppDbTable().SetResponseForKey(
      "vrf-0", "SWSS_RC_INVALID_PARAM", "my error message");

  // We expect the invalid argument error to be propagated all the way back to
  // the gRPC response.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("#1: INVALID_ARGUMENT: my error message")));

  // Sanity check that the entry is not accidentally left in the VRF_TABLE.
  ASSERT_THAT(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("vrf-0"),
              StatusIs(absl::StatusCode::kNotFound))
      << "VRF ID was not cleaned up after failure.";
}

TEST_F(VrfTableTest, CannotDeleteMissingEntry) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: DELETE
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: NOT_FOUND")));
}

TEST_F(VrfTableTest, DeleteRequestFails) {
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::IrWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 table_name: "vrf_table"
                                 matches {
                                   name: "vrf_id"
                                   exact { str: "vrf-0" }
                                 }
                                 action { name: "no_action" }
                               }
                             })pb",
                           ir_p4_info_));

  // First we insert the entry because a delete isn't allowed on non-existing
  // table entries.
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));

  // Then we can update the PI write request to delete the entry, and setup the
  // Orchagent to fail with an invalid parameter.
  request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  p4rt_service_.GetVrfAppDbTable().SetResponseForKey(
      "vrf-0", "SWSS_RC_INVALID_PARAM", "my error message");

  // We expect the invalid argument error to be propagated all the way back to
  // the gRPC response.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown,
               HasSubstr("#1: INVALID_ARGUMENT: my error message")));

  // Sanity check that the entry sitll exists in the VRF_TABLE.
  ASSERT_OK(p4rt_service_.GetVrfAppDbTable().ReadTableEntry("vrf-0"))
      << "VRF ID was not re-inserted after failure.";
}

}  // namespace
}  // namespace p4rt_app

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
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

class FixedL3TableTest : public testing::Test {
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
    p4rt_service_.GetPortAppDbTable().InsertTableEntry("Ethernet4",
                                                       {{"id", "2"}});

    // Push a P4Info file to enable the reading, and writing of entries.
    ASSERT_OK(pdpi::SetForwardingPipelineConfig(
        p4rt_session_.get(), sai::GetP4Info(sai::SwitchRole::kMiddleblock)));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(FixedL3TableTest, SupportRouterInterfaceTableFlows) {
  // P4 write request.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554497
                 match {
                   field_id: 1
                   exact { value: "16" }
                 }
                 action {
                   action {
                     action_id: 16777218
                     params { param_id: 1 value: "2" }
                     params { param_id: 2 value: "\002\003\004\005\006" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_ROUTER_INTERFACE_TABLE")
                            .AddMatchField("router_interface_id", "16")
                            .SetAction("set_port_and_src_mac")
                            .AddActionParam("port", "Ethernet4")
                            .AddActionParam("src_mac", "00:02:03:04:05:06");

  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that port_id_t translations are read back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetIdAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

TEST_F(FixedL3TableTest, SupportNeighborAndNexthopTableFlows) {
  // P4 write request.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(# ----- neighbor entry -----
           updates {
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
           }
           # ----- nexthop entry -----
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
                     params { param_id: 2 value: "fe80::021a:11ff:fe17:5f80" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // Expected P4RT AppDb entries.
  auto neighbor_entry =
      test_lib::AppDbEntryBuilder{}
          .SetTableName("FIXED_NEIGHBOR_TABLE")
          .AddMatchField("neighbor_id", "fe80::021a:11ff:fe17:5f80")
          .AddMatchField("router_interface_id", "1")
          .SetAction("set_dst_mac")
          .AddActionParam("dst_mac", "00:1a:11:17:5f:80");
  auto nexthop_entry =
      test_lib::AppDbEntryBuilder{}
          .SetTableName("FIXED_NEXTHOP_TABLE")
          .AddMatchField("nexthop_id", "8")
          .SetAction("set_nexthop")
          .AddActionParam("router_interface_id", "8")
          .AddActionParam("neighbor_id", "fe80::021a:11ff:fe17:5f80");

  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(neighbor_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(neighbor_entry.GetValueMap())));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(nexthop_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(nexthop_entry.GetValueMap())));
}

TEST_F(FixedL3TableTest, SupportIpv4TableFlow) {
  // P4 write request.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554500
                 match {
                   field_id: 1
                   exact { value: "50" }
                 }
                 match {
                   field_id: 2
                   lpm { value: "\nQ\010\000" prefix_len: 23 }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "8" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV4_TABLE")
                            .AddMatchField("ipv4_dst", "10.81.8.0/23")
                            .AddMatchField("vrf_id", "p4rt-50")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "8");

  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that vrf_id_t translations are read back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetIdAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

TEST_F(FixedL3TableTest, SupportIpv6TableFlow) {
  // P4 write request.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "80" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV6_TABLE")
                            .AddMatchField("ipv6_dst", "2002:a17:506:c114::/64")
                            .AddMatchField("vrf_id", "p4rt-80")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));
}

TEST_F(FixedL3TableTest, TableEntryInsertReadAndRemove) {
  // P4 write request.
  p4::v1::WriteRequest write_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "80" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &write_request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV6_TABLE")
                            .AddMatchField("ipv6_dst", "2002:a17:506:c114::/64")
                            .AddMatchField("vrf_id", "p4rt-80")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  // The insert write request should not fail, and once complete the entry
  // should exist in the P4RT AppDb table.
  ASSERT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Reading back the entry should result in the same table_entry.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetIdAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(write_request.updates(0).entity()));

  // Modify the P4 write request to delete the entry.
  write_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);

  // The delete write request should not fail, and once complete the entry
  // should no longer exist in the P4RT AppDb table.
  ASSERT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      StatusIs(absl::StatusCode::kNotFound));

  // Reading back the entry should result in nothing being returned.
  ASSERT_OK_AND_ASSIGN(read_response, pdpi::SetIdAndSendPiReadRequest(
                                          p4rt_session_.get(), read_request));
  EXPECT_EQ(read_response.entities_size(), 0);
}

TEST_F(FixedL3TableTest, TableEntryModify) {
  // P4 write request.
  p4::v1::WriteRequest write_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "80" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &write_request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV6_TABLE")
                            .AddMatchField("ipv6_dst", "2002:a17:506:c114::/64")
                            .AddMatchField("vrf_id", "p4rt-80");

  // The insert write request should not fail, and once complete the entry
  // should exist in the P4RT AppDb table.
  ASSERT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request));
  ASSERT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(
          UnorderedElementsAre(std::make_pair("action", "set_nexthop_id"),
                               std::make_pair("param/nexthop_id", "20"))));

  // Update nexthop_id to a new value and send the modify request. Once complete
  // the entry should have a new nexthop_id.
  write_request.mutable_updates(0)->set_type(p4::v1::Update::MODIFY);
  *write_request.mutable_updates(0)
       ->mutable_entity()
       ->mutable_table_entry()
       ->mutable_action()
       ->mutable_action()
       ->mutable_params(0)
       ->mutable_value() = "30";
  ASSERT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(
          UnorderedElementsAre(std::make_pair("action", "set_nexthop_id"),
                               std::make_pair("param/nexthop_id", "30"))));
}

TEST_F(FixedL3TableTest, DuplicateTableEntryInsertFails) {
  // P4 write request.
  p4::v1::WriteRequest write_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "80" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &write_request));

  // The first insert is expected to pass since the entry does not exist.
  EXPECT_OK(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request));

  // The second insert is expected to fail since the entry already exists.
  EXPECT_THAT(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("ALREADY_EXISTS")));
}

TEST_F(FixedL3TableTest, TableEntryModifyFailsIfEntryDoesNotExist) {
  // P4 write request.
  p4::v1::WriteRequest write_request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: MODIFY
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "80" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &write_request));
  EXPECT_THAT(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), write_request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("NOT_FOUND")));
}

TEST_F(FixedL3TableTest, InvalidPortIdFails) {
  // P4 write request for the router interface table. Action parameter 1 is the
  // port, and we give it an unassigned value (i.e. 999).
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554497
                 match {
                   field_id: 1
                   exact { value: "16" }
                 }
                 action {
                   action {
                     action_id: 16777218
                     params { param_id: 1 value: "999" }
                     params { param_id: 2 value: "\002\003\004\005\006" }
                   }
                 }
               }
             }
           })pb",
      &request));

  EXPECT_THAT(
      pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

// TODO: remove special handling when ONF no longer relies on it.
TEST_F(FixedL3TableTest, SupportDefaultVrf) {
  // P4 write request.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554501
                 match {
                   field_id: 1
                   exact { value: "vrf-0" }
                 }
                 match {
                   field_id: 2
                   lpm {
                     value: " \002\n\027\005\006\301\024\000\000\000\000\000\000\000\000"
                     prefix_len: 64
                   }
                 }
                 action {
                   action {
                     action_id: 16777221
                     params { param_id: 1 value: "20" }
                   }
                 }
               }
             }
           })pb",
      &request));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV6_TABLE")
                            .AddMatchField("ipv6_dst", "2002:a17:506:c114::/64")
                            .AddMatchField("vrf_id", "")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  EXPECT_OK(pdpi::SetIdsAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that the default vrf is translated back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetIdAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

}  // namespace
}  // namespace p4rt_app

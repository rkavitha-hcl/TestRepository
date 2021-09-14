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
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/ir.pb.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/tests/lib/p4runtime_component_test_fixture.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::EqualsProto;
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;
using ::testing::UnorderedElementsAre;
using ::testing::UnorderedElementsAreArray;

// Ensure we can program each of the L3 flows.
class FixedL3TableTest : public test_lib::P4RuntimeComponentTestFixture {
 protected:
  FixedL3TableTest()
      : test_lib::P4RuntimeComponentTestFixture(
            sai::Instantiation::kMiddleblock,
            /*gnmi_ports=*/{
                test_lib::FakeGnmiPortConfig{
                    .port_id = "1",
                    .port_name = "Ethernet0",
                },
                test_lib::FakeGnmiPortConfig{
                    .port_id = "2",
                    .port_name = "Ethernet4",
                },
            }) {}
};

TEST_F(FixedL3TableTest, SupportRouterInterfaceTableFlows) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 router_interface_table_entry {
                                   match { router_interface_id: "16" }
                                   action {
                                     set_port_and_src_mac {
                                       port: "2"
                                       src_mac: "00:02:03:04:05:06"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_ROUTER_INTERFACE_TABLE")
                            .AddMatchField("router_interface_id", "16")
                            .SetAction("set_port_and_src_mac")
                            .AddActionParam("port", "Ethernet4")
                            .AddActionParam("src_mac", "00:02:03:04:05:06");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that port_id_t translations are read back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

TEST_F(FixedL3TableTest, SupportNeighborTableFlows) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                neighbor_table_entry {
                  match {
                    neighbor_id: "fe80::021a:11ff:fe17:5f80"
                    router_interface_id: "1"
                  }
                  action { set_dst_mac { dst_mac: "00:1a:11:17:5f:80" } }
                }
              }
            }
          )pb",
          ir_p4_info_));

  // Expected P4RT AppDb entries.
  auto neighbor_entry =
      test_lib::AppDbEntryBuilder{}
          .SetTableName("FIXED_NEIGHBOR_TABLE")
          .AddMatchField("neighbor_id", "fe80::021a:11ff:fe17:5f80")
          .AddMatchField("router_interface_id", "1")
          .SetAction("set_dst_mac")
          .AddActionParam("dst_mac", "00:1a:11:17:5f:80");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(neighbor_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(neighbor_entry.GetValueMap())));
}

TEST_F(FixedL3TableTest, SupportNexthopTableFlows) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 nexthop_table_entry {
                                   match { nexthop_id: "8" }
                                   action {
                                     set_nexthop {
                                       router_interface_id: "8"
                                       neighbor_id: "fe80::021a:11ff:fe17:5f80"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));

  // Expected P4RT AppDb entries.
  auto nexthop_entry =
      test_lib::AppDbEntryBuilder{}
          .SetTableName("FIXED_NEXTHOP_TABLE")
          .AddMatchField("nexthop_id", "8")
          .SetAction("set_nexthop")
          .AddActionParam("router_interface_id", "8")
          .AddActionParam("neighbor_id", "fe80::021a:11ff:fe17:5f80");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(nexthop_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(nexthop_entry.GetValueMap())));
}

TEST_F(FixedL3TableTest, SupportIpv4TableFlow) {
  // P4 write request.
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

  // Expected P4RT AppDb entry.
  auto expected_entry = test_lib::AppDbEntryBuilder{}
                            .SetTableName("FIXED_IPV4_TABLE")
                            .AddMatchField("ipv4_dst", "10.81.8.0/23")
                            .AddMatchField("vrf_id", "p4rt-50")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "8");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Sanity check that vrf_id_t translations are read back correctly.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(request.updates(0).entity()));
}

TEST_F(FixedL3TableTest, SupportIpv6TableFlow) {
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
                    vrf_id: "80"
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
                            .AddMatchField("vrf_id", "p4rt-80")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));
}

TEST_F(FixedL3TableTest, TableEntryInsertReadAndRemove) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest write_request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv6_table_entry {
                  match {
                    vrf_id: "80"
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
                            .AddMatchField("vrf_id", "p4rt-80")
                            .SetAction("set_nexthop_id")
                            .AddActionParam("nexthop_id", "20");

  // The insert write request should not fail, and once complete the entry
  // should exist in the P4RT AppDb table.
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(UnorderedElementsAreArray(expected_entry.GetValueMap())));

  // Reading back the entry should result in the same table_entry.
  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(p4rt_session_.get(), read_request));
  ASSERT_EQ(read_response.entities_size(), 1);  // Only one write.
  EXPECT_THAT(read_response.entities(0),
              EqualsProto(write_request.updates(0).entity()));

  // Modify the P4 write request to delete the entry. We should be able to
  // delete the entry with only the match key.
  write_request.mutable_updates(0)->set_type(p4::v1::Update::DELETE);
  write_request.mutable_updates(0)
      ->mutable_entity()
      ->mutable_table_entry()
      ->mutable_action()
      ->Clear();

  // The delete write request should not fail, and once complete the entry
  // should no longer exist in the P4RT AppDb table.
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      StatusIs(absl::StatusCode::kNotFound));

  // Reading back the entry should result in nothing being returned.
  ASSERT_OK_AND_ASSIGN(read_response, pdpi::SetMetadataAndSendPiReadRequest(
                                          p4rt_session_.get(), read_request));
  EXPECT_EQ(read_response.entities_size(), 0);
}

TEST_F(FixedL3TableTest, TableEntryModify) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest write_request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv6_table_entry {
                  match {
                    vrf_id: "80"
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
                            .AddMatchField("vrf_id", "p4rt-80");

  // The insert write request should not fail, and once complete the entry
  // should exist in the P4RT AppDb table.
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));
  ASSERT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(
          UnorderedElementsAre(std::make_pair("action", "set_nexthop_id"),
                               std::make_pair("param/nexthop_id", "20"))));

  // Update the request with a new action.
  write_request.mutable_updates(0)->set_type(p4::v1::Update::MODIFY);
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(action {
             action_id: 16777220
             params { param_id: 1 value: "30" }
           })pb",
      write_request.mutable_updates(0)
          ->mutable_entity()
          ->mutable_table_entry()
          ->mutable_action()));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));
  EXPECT_THAT(
      p4rt_service_.GetP4rtAppDbTable().ReadTableEntry(expected_entry.GetKey()),
      IsOkAndHolds(
          UnorderedElementsAre(std::make_pair("action", "set_wcmp_group_id"),
                               std::make_pair("param/wcmp_group_id", "30"))));
}

TEST_F(FixedL3TableTest, DuplicateTableEntryInsertFails) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest write_request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: INSERT
              table_entry {
                ipv6_table_entry {
                  match {
                    vrf_id: "80"
                    ipv6_dst { value: "2002:a17:506:c114::" prefix_length: 64 }
                  }
                  action { set_nexthop_id { nexthop_id: "20" } }
                }
              }
            }
          )pb",
          ir_p4_info_));

  // The first insert is expected to pass since the entry does not exist.
  EXPECT_OK(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                   write_request));

  // The second insert is expected to fail since the entry already exists.
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                             write_request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("ALREADY_EXISTS")));
}

TEST_F(FixedL3TableTest, TableEntryModifyFailsIfEntryDoesNotExist) {
  // P4 write request.
  ASSERT_OK_AND_ASSIGN(
      p4::v1::WriteRequest write_request,
      test_lib::PdWriteRequestToPi(
          R"pb(
            updates {
              type: MODIFY
              table_entry {
                ipv6_table_entry {
                  match {
                    vrf_id: "80"
                    ipv6_dst { value: "2002:a17:506:c114::" prefix_length: 64 }
                  }
                  action { set_nexthop_id { nexthop_id: "20" } }
                }
              }
            }
          )pb",
          ir_p4_info_));
  EXPECT_THAT(pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(),
                                                     write_request),
              StatusIs(absl::StatusCode::kUnknown, HasSubstr("NOT_FOUND")));
}

TEST_F(FixedL3TableTest, InvalidPortIdFails) {
  // P4 write request with an unassigned port value (i.e. 999).
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 router_interface_table_entry {
                                   match { router_interface_id: "16" }
                                   action {
                                     set_port_and_src_mac {
                                       port: "999"
                                       src_mac: "00:02:03:04:05:06"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));

  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

// TODO: remove special handling when ONF no longer relies on it.
TEST_F(FixedL3TableTest, SupportDefaultVrf) {
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

TEST_F(FixedL3TableTest, IncorrectlyFormatedRequestFailsConstraintCheck) {
  // P4 write request for the "router interface table", but its match field is
  // missing a value making it invalid.
  p4::v1::WriteRequest request;
  ASSERT_OK(gutil::ReadProtoFromString(
      R"pb(updates {
             type: INSERT
             entity {
               table_entry {
                 table_id: 33554497
                 match { field_id: 1 }
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

  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

}  // namespace
}  // namespace p4rt_app

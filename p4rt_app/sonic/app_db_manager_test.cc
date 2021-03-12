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
#include "p4rt_app/sonic/app_db_manager.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/memory/memory.h"
#include "absl/status/statusor.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4rt_app/tests/lib/app_db_entry_builder.h"
#include "p4rt_app/utils/table_utility.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "swss/mocks/mock_consumer_notifier.h"
#include "swss/mocks/mock_db_connector.h"
#include "swss/mocks/mock_producer_state_table.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::google::protobuf::TextFormat;
using ::gutil::EqualsProto;
using ::gutil::StatusIs;
using ::p4rt_app::test_lib::AppDbEntryBuilder;
using ::testing::_;
using ::testing::ContainerEq;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::Return;
using ::testing::SetArgReferee;

const std::vector<std::pair<std::string, std::string>>&
GetSuccessfulResponseValues() {
  static const std::vector<std::pair<std::string, std::string>>* const
      kResponse = new std::vector<std::pair<std::string, std::string>>{
          {"err_str", "SWSS_RC_SUCCESS"}};
  return *kResponse;
}

class AppDbManagerTest : public ::testing::Test {
 protected:
  AppDbManagerTest() {
    ON_CALL(mock_p4rt_table_, get_table_name)
        .WillByDefault(Return(p4rt_table_name_));
    ON_CALL(mock_vrf_table_, get_table_name)
        .WillByDefault(Return(vrf_table_name_));
  }

  const std::string p4rt_table_name_ = "P4RT";
  const std::string vrf_table_name_ = "VRF_TABLE";
  // Mock AppDb tables.
  swss::MockDBConnector mock_app_db_client_;
  swss::MockProducerStateTable mock_p4rt_table_;
  swss::MockConsumerNotifier mock_p4rt_notification_;
  swss::MockProducerStateTable mock_vrf_table_;
  swss::MockConsumerNotifier mock_vrf_notification_;
  // Mock StateDb tables.
  swss::MockDBConnector mock_state_db_client_;

  absl::flat_hash_map<std::string, int> vrf_id_reference_count_;
};

TEST_F(AppDbManagerTest, InsertTableEntry) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(R"pb(
                                    table_name: "router_interface_table"
                                    priority: 123
                                    matches {
                                      name: "router_interface_id"
                                      exact { hex_str: "16" }
                                    }
                                    action {
                                      name: "set_port_and_src_mac"
                                      params {
                                        name: "port"
                                        value { str: "Ethernet28/5" }
                                      }
                                      params {
                                        name: "src_mac"
                                        value { mac: "00:02:03:04:05:06" }
                                      }
                                    })pb",
                                  &table_entry));
  AppDbUpdates updates;
  updates.entries.push_back(AppDbEntry{.rpc_index = 0,
                                       .entry = table_entry,
                                       .update_type = p4::v1::Update::INSERT});
  updates.total_rpc_updates = 1;

  // Expected RedisDB entry.
  const auto expected = AppDbEntryBuilder{}
                            .SetTableName("FIXED_ROUTER_INTERFACE_TABLE")
                            .SetPriority(123)
                            .AddMatchField("router_interface_id", "16")
                            .SetAction("set_port_and_src_mac")
                            .AddActionParam("port", "Ethernet28/5")
                            .AddActionParam("src_mac", "00:02:03:04:05:06");
  EXPECT_CALL(mock_p4rt_table_,
              set(Eq(expected.GetKey()), expected.GetValueList(), _, _))
      .Times(1);

  // Expected OrchAgent response.
  EXPECT_CALL(mock_p4rt_notification_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>(expected.GetKey()),
                      SetArgReferee<2>(GetSuccessfulResponseValues()),
                      Return(true)));

  pdpi::IrWriteResponse response;
  EXPECT_OK(
      UpdateAppDb(updates, sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
                  mock_p4rt_table_, mock_p4rt_notification_,
                  mock_app_db_client_, mock_state_db_client_, mock_vrf_table_,
                  mock_vrf_notification_, &vrf_id_reference_count_, &response));
  ASSERT_EQ(response.statuses_size(), 1);
  EXPECT_EQ(response.statuses(0).code(), google::rpc::OK);
}

TEST_F(AppDbManagerTest, InsertDuplicateTableEntryFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(R"pb(
                                    table_name: "router_interface_table"
                                    priority: 123
                                    matches {
                                      name: "router_interface_id"
                                      exact { hex_str: "16" }
                                    }
                                    action {
                                      name: "set_port_and_src_mac"
                                      params {
                                        name: "port"
                                        value { str: "Ethernet28/5" }
                                      }
                                      params {
                                        name: "src_mac"
                                        value { mac: "00:02:03:04:05:06" }
                                      }
                                    })pb",
                                  &table_entry));
  AppDbUpdates updates;
  updates.entries.push_back(AppDbEntry{.rpc_index = 0,
                                       .entry = table_entry,
                                       .update_type = p4::v1::Update::INSERT});
  updates.total_rpc_updates = 1;

  // RedisDB returns that the entry already exists.
  const auto expected = AppDbEntryBuilder{}
                            .SetTableName("P4RT:FIXED_ROUTER_INTERFACE_TABLE")
                            .SetPriority(123)
                            .AddMatchField("router_interface_id", "16");
  EXPECT_CALL(mock_app_db_client_, exists(Eq(expected.GetKey())))
      .WillOnce(Return(true));

  pdpi::IrWriteResponse response;
  response.add_statuses();
  EXPECT_OK(
      UpdateAppDb(updates, sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
                  mock_p4rt_table_, mock_p4rt_notification_,
                  mock_app_db_client_, mock_state_db_client_, mock_vrf_table_,
                  mock_vrf_notification_, &vrf_id_reference_count_, &response));
  EXPECT_EQ(response.statuses(0).code(), google::rpc::ALREADY_EXISTS);
}

TEST_F(AppDbManagerTest, ModifyNonExistentTableEntryFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(R"pb(
                                    table_name: "router_interface_table"
                                    priority: 123
                                    matches {
                                      name: "router_interface_id"
                                      exact { hex_str: "16" }
                                    }
                                    action {
                                      name: "set_port_and_src_mac"
                                      params {
                                        name: "port"
                                        value { str: "Ethernet28/5" }
                                      }
                                      params {
                                        name: "src_mac"
                                        value { mac: "00:02:03:04:05:06" }
                                      }
                                    })pb",
                                  &table_entry));
  AppDbUpdates updates;
  updates.entries.push_back(AppDbEntry{.rpc_index = 0,
                                       .entry = table_entry,
                                       .update_type = p4::v1::Update::MODIFY});
  updates.total_rpc_updates = 1;

  // RedisDB returns that the entry does not exists.
  const auto expected = AppDbEntryBuilder{}
                            .SetTableName("P4RT:FIXED_ROUTER_INTERFACE_TABLE")
                            .SetPriority(123)
                            .AddMatchField("router_interface_id", "16");
  EXPECT_CALL(mock_app_db_client_, exists(Eq(expected.GetKey())))
      .WillOnce(Return(false));

  pdpi::IrWriteResponse response;
  response.add_statuses();
  EXPECT_OK(
      UpdateAppDb(updates, sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
                  mock_p4rt_table_, mock_p4rt_notification_,
                  mock_app_db_client_, mock_state_db_client_, mock_vrf_table_,
                  mock_vrf_notification_, &vrf_id_reference_count_, &response));
  EXPECT_EQ(response.statuses(0).code(), google::rpc::NOT_FOUND);
}

TEST_F(AppDbManagerTest, DeleteNonExistentTableEntryFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(R"pb(
                                    table_name: "router_interface_table"
                                    priority: 123
                                    matches {
                                      name: "router_interface_id"
                                      exact { hex_str: "16" }
                                    }
                                    action {
                                      name: "set_port_and_src_mac"
                                      params {
                                        name: "port"
                                        value { str: "Ethernet28/5" }
                                      }
                                      params {
                                        name: "src_mac"
                                        value { mac: "00:02:03:04:05:06" }
                                      }
                                    })pb",
                                  &table_entry));
  AppDbUpdates updates;
  updates.entries.push_back(AppDbEntry{.rpc_index = 0,
                                       .entry = table_entry,
                                       .update_type = p4::v1::Update::DELETE});
  updates.total_rpc_updates = 1;

  // RedisDB returns that the entry does not exists.
  const auto expected = AppDbEntryBuilder{}
                            .SetTableName("P4RT:FIXED_ROUTER_INTERFACE_TABLE")
                            .SetPriority(123)
                            .AddMatchField("router_interface_id", "16");
  EXPECT_CALL(mock_app_db_client_, exists(Eq(expected.GetKey())))
      .WillOnce(Return(false));

  pdpi::IrWriteResponse response;
  response.add_statuses();
  EXPECT_OK(
      UpdateAppDb(updates, sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
                  mock_p4rt_table_, mock_p4rt_notification_,
                  mock_app_db_client_, mock_state_db_client_, mock_vrf_table_,
                  mock_vrf_notification_, &vrf_id_reference_count_, &response));
  EXPECT_EQ(response.statuses(0).code(), google::rpc::NOT_FOUND);
}

TEST_F(AppDbManagerTest, ReadAppDbP4TableEntry) {
  const auto app_db_entry =
      AppDbEntryBuilder{}
          .SetTableName("P4RT:FIXED_ROUTER_INTERFACE_TABLE")
          .SetPriority(123)
          .AddMatchField("router_interface_id", "16")
          .SetAction("set_port_and_src_mac")
          .AddActionParam("port", "Ethernet28/5")
          .AddActionParam("src_mac", "00:02:03:04:05:06");

  swss::MockDBConnector mock_redis_client;
  EXPECT_CALL(mock_redis_client, hgetall(Eq(app_db_entry.GetKey())))
      .WillOnce(Return(app_db_entry.GetValueMap()));

  auto table_entry_status =
      ReadAppDbP4TableEntry(sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
                            mock_redis_client, app_db_entry.GetKey());
  ASSERT_TRUE(table_entry_status.ok()) << table_entry_status.status();
  pdpi::IrTableEntry table_entry = table_entry_status.value();

  EXPECT_THAT(table_entry, EqualsProto(R"pb(
                table_name: "router_interface_table"
                priority: 123
                matches {
                  name: "router_interface_id"
                  exact { str: "16" }
                }
                action {
                  name: "set_port_and_src_mac"
                  params {
                    name: "port"
                    value { str: "Ethernet28/5" }
                  }
                  params {
                    name: "src_mac"
                    value { mac: "00:02:03:04:05:06" }
                  }
                })pb"));
}

TEST_F(AppDbManagerTest, GetAllP4KeysReturnsInstalledKeys) {
  swss::MockDBConnector mock_redis_client;
  EXPECT_CALL(mock_redis_client, keys)
      .WillOnce(Return(std::vector<std::string>{"P4RT:TABLE:{key}"}));

  EXPECT_THAT(GetAllAppDbP4TableEntryKeys(mock_redis_client),
              ContainerEq(std::vector<std::string>{"P4RT:TABLE:{key}"}));
}

TEST_F(AppDbManagerTest, GetAllP4KeysDoesNotReturnUninstalledKey) {
  swss::MockDBConnector mock_redis_client;
  EXPECT_CALL(mock_redis_client, keys)
      .WillOnce(Return(std::vector<std::string>{"_P4RT:TABLE:{key}"}));

  EXPECT_THAT(GetAllAppDbP4TableEntryKeys(mock_redis_client),
              ContainerEq(std::vector<std::string>{}));
}

TEST_F(AppDbManagerTest, GetAllP4KeysIgnoresKeySet) {
  swss::MockDBConnector mock_redis_client;
  EXPECT_CALL(mock_redis_client, keys)
      .WillOnce(Return(
          std::vector<std::string>{"P4RT_KEY_SET:TABLE", "P4RT:TABLE:{key}"}));

  EXPECT_THAT(GetAllAppDbP4TableEntryKeys(mock_redis_client),
              ContainerEq(std::vector<std::string>{"P4RT:TABLE:{key}"}));
}

TEST_F(AppDbManagerTest, GetAllP4KeysIgnoresDelSet) {
  swss::MockDBConnector mock_redis_client;
  EXPECT_CALL(mock_redis_client, keys)
      .WillOnce(Return(
          std::vector<std::string>{"P4RT_DEL_SET:TABLE", "P4RT:TABLE:{key}"}));

  EXPECT_THAT(GetAllAppDbP4TableEntryKeys(mock_redis_client),
              ContainerEq(std::vector<std::string>{"P4RT:TABLE:{key}"}));
}

TEST(PortIdTranslationTest, GetMap) {
  swss::MockDBConnector mock_db_connector;

  // We will first check the database for any Ethernet entries in the
  // PORT_TABLE.
  EXPECT_CALL(mock_db_connector, keys)
      .WillOnce(Return(std::vector<std::string>{"PORT_TABLE:Ethernet0",
                                                "PORT_TABLE:Ethernet4"}));

  // Then for each entry we will check it's ID value.
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet0"))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "1"}}));
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet4"))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "2"}}));

  ASSERT_OK_AND_ASSIGN(auto translation_map,
                       GetPortIdTranslationMap(mock_db_connector));

  // bimap Ethernet0 <=> 1
  EXPECT_EQ(translation_map.left.at("Ethernet0"), "1");
  EXPECT_EQ(translation_map.right.at("1"), "Ethernet0");

  // bimap Ethernet4 <=> 2
  EXPECT_EQ(translation_map.left.at("Ethernet4"), "2");
  EXPECT_EQ(translation_map.right.at("2"), "Ethernet4");
}

TEST(PortIdTranslationTest, MissingPortIdsFails) {
  swss::MockDBConnector mock_db_connector;

  // When we check the redis DB for Ethernet4's port ID it returns an empty
  // list.
  EXPECT_CALL(mock_db_connector, keys)
      .WillOnce(Return(std::vector<std::string>{"PORT_TABLE:Ethernet4"}));
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet4"))
      .WillOnce(Return(std::unordered_map<std::string, std::string>{}));
  EXPECT_THAT(GetPortIdTranslationMap(mock_db_connector),
              StatusIs(absl::StatusCode::kInternal));
}

TEST(PortIdTranslationTest, DuplicatePortIdsFails) {
  swss::MockDBConnector mock_db_connector;

  // We will first check the database for any Ethernet entries in the
  // PORT_TABLE.
  EXPECT_CALL(mock_db_connector, keys)
      .WillOnce(Return(std::vector<std::string>{"PORT_TABLE:Ethernet0",
                                                "PORT_TABLE:Ethernet4"}));

  // Then for each entry we will check it's ID value.
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet0"))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "1"}}));
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet4"))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "1"}}));

  // Because Ethernet0 and Ethernet4 both have ID 1 the mapping fails.
  EXPECT_THAT(GetPortIdTranslationMap(mock_db_connector),
              StatusIs(absl::StatusCode::kInternal));
}

// This test is likely breaking an invariant in redis (i.e. multiple table
// entries with the same key). However, we're keeping it to ensure P4RT App
// cleanly handles the case.
TEST(PortIdTranslationTest, DuplicatePortNamesFails) {
  swss::MockDBConnector mock_db_connector;

  // We will first check the database for any Ethernet entries in the
  // PORT_TABLE.
  EXPECT_CALL(mock_db_connector, keys)
      .WillOnce(Return(std::vector<std::string>{"PORT_TABLE:Ethernet0",
                                                "PORT_TABLE:Ethernet0"}));

  // Then for each entry we will check it's ID value.
  EXPECT_CALL(mock_db_connector, hgetall("PORT_TABLE:Ethernet0"))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "1"}}))
      .WillOnce(
          Return(std::unordered_map<std::string, std::string>{{"id", "2"}}));

  // Because Ethernet0 is used twice the mapping fails.
  EXPECT_THAT(GetPortIdTranslationMap(mock_db_connector),
              StatusIs(absl::StatusCode::kInternal));
}

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

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
#include "p4rt_app/sonic/vrf_entry_translation.h"

#include "absl/container/flat_hash_map.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "swss/mocks/mock_consumer_notifier.h"
#include "swss/mocks/mock_db_connector.h"
#include "swss/mocks/mock_producer_state_table.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::google::protobuf::TextFormat;
using ::gutil::StatusIs;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::Return;
using ::testing::SetArgReferee;

class VrfEntryTranslationTest : public ::testing::Test {
 protected:
  VrfEntryTranslationTest() {
    ON_CALL(mock_vrf_table_, get_table_name)
        .WillByDefault(Return(vrf_table_name_));
  }

  const std::string vrf_table_name_ = "VRF_TABLE";
  swss::MockProducerStateTable mock_vrf_table_;
  swss::MockConsumerNotifier mock_vrf_notifier_;
  swss::MockDBConnector mock_app_db_client_;
  swss::MockDBConnector mock_state_db_client_;
  absl::flat_hash_map<std::string, int> vrf_id_reference_count_;
};

TEST_F(VrfEntryTranslationTest, InsertIgnoresRequestWithoutVrfId) {
  pdpi::IrTableEntry table_entry;

  EXPECT_CALL(mock_vrf_table_, set(_)).Times(0);
  EXPECT_OK(InsertVrfEntryAndUpdateState(
      mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
      mock_state_db_client_, table_entry, vrf_id_reference_count_));
  EXPECT_TRUE(vrf_id_reference_count_.empty());
}

TEST_F(VrfEntryTranslationTest, InsertVrfIdFromMatchField) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-1"), _, _, _)).Times(1);
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(InsertVrfEntryAndUpdateState(
      mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
      mock_state_db_client_, table_entry, vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, InsertVrfIdFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-1"), _, _, _)).Times(1);
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_INTERNAL"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "my error")})),
                      Return(true)));

  // Return empty appdb values to indicate that this entry does not exist
  // before.
  EXPECT_CALL(mock_state_db_client_, hgetall("VRF_TABLE:vrf-1"))
      .Times(1)
      .WillRepeatedly(Return(std::unordered_map<std::string, std::string>()));
  // Expect the newly inserted entry in APP_DB to be deleted.
  EXPECT_CALL(mock_app_db_client_, del("VRF_TABLE:vrf-1"))
      .WillOnce(Return(/*value=*/1));
  EXPECT_THAT(InsertVrfEntryAndUpdateState(
                  mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
                  mock_state_db_client_, table_entry, vrf_id_reference_count_),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("my error")));
}

TEST_F(VrfEntryTranslationTest, InsertVrfIdFromActionParam) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-2" }
                                                 }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-2"), _, _, _)).Times(1);
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-2"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(InsertVrfEntryAndUpdateState(
      mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
      mock_state_db_client_, table_entry, vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, DuplicateInserts) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  // Redis DB is only called for unique values. Since we are inserting the same
  // entry we only expect the DB to be called once.
  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-1"), _, _, _)).Times(1);
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  // First insert call will update the Redis DB, and reference count.
  EXPECT_OK(InsertVrfEntryAndUpdateState(
      mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
      mock_state_db_client_, table_entry, vrf_id_reference_count_));
  EXPECT_EQ(vrf_id_reference_count_["vrf-1"], 1);

  // Second insert call will update only the reference count.
  EXPECT_OK(InsertVrfEntryAndUpdateState(
      mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
      mock_state_db_client_, table_entry, vrf_id_reference_count_));
  EXPECT_EQ(vrf_id_reference_count_["vrf-1"], 2);
}

TEST_F(VrfEntryTranslationTest, DeleteIgnoresRequestWithoutVrfId) {
  pdpi::IrTableEntry table_entry;

  EXPECT_CALL(mock_vrf_table_, del(_)).Times(0);
  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));
  EXPECT_TRUE(vrf_id_reference_count_.empty());
}

TEST_F(VrfEntryTranslationTest, DeleteVrfIdFromMatchField) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-1"), _, _)).Times(1);
  vrf_id_reference_count_["vrf-1"] = 1;

  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));

  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                               mock_app_db_client_, mock_state_db_client_,
                               vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, DeleteVrfIdFromMatchFieldFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-1"), _, _)).Times(1);
  vrf_id_reference_count_["vrf-1"] = 1;

  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));

  // Fake Orchagent error response for delete vrf.
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_INTERNAL"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "my error")})),
                      Return(true)));

  // Mock state db to return previous value of this vrf.
  EXPECT_CALL(mock_state_db_client_, hgetall("VRF_TABLE:vrf-1"))
      .Times(1)
      .WillRepeatedly(Return(std::unordered_map<std::string, std::string>(
          {std::make_pair("vrf-1", "1")})));
  // Expect app db to be inserted back with the deleted vrf entry.
  EXPECT_CALL(mock_app_db_client_, hmset).Times(1);

  EXPECT_THAT(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                                 mock_app_db_client_, mock_state_db_client_,
                                 vrf_id_reference_count_),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(VrfEntryTranslationTest, DeleteVrfIdFromActionParam) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-2" }
                                                 }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-2"), _, _)).Times(1);
  vrf_id_reference_count_["vrf-2"] = 1;
  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-2"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                               mock_app_db_client_, mock_state_db_client_,
                               vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, DeleteNonExistantVrfIdFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-2" }
                                                 }
                                               })pb",
                                          &table_entry));

  EXPECT_CALL(mock_vrf_table_, del(_)).Times(0);
  EXPECT_THAT(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                         vrf_id_reference_count_),
              StatusIs(absl::StatusCode::kInternal));
}

TEST_F(VrfEntryTranslationTest, DuplicateDeletes) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(matches {
                                                 name: "vrf_id"
                                                 exact { str: "vrf-1" }
                                               })pb",
                                          &table_entry));

  // Redis DB is only called when the ID is no longer referenced. Since we're
  // deleting the same key we expect the DB to be called once.
  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-1"), _, _)).Times(1);
  vrf_id_reference_count_["vrf-1"] = 2;

  // First delete call will only update the reference count.
  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));
  EXPECT_EQ(vrf_id_reference_count_["vrf-1"], 1);

  // Second delete call will update the reference count and the DB.
  EXPECT_OK(DecrementVrfReferenceCount(mock_vrf_table_, table_entry,
                                       vrf_id_reference_count_));
  EXPECT_EQ(vrf_id_reference_count_["vrf-1"], 0);

  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                               mock_app_db_client_, mock_state_db_client_,
                               vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, ModifyIgnoresRequestWithoutVrfId) {
  pdpi::IrTableEntry table_entry;
  std::unordered_map<std::string, std::string> app_db_values;

  EXPECT_CALL(mock_vrf_table_, del(_)).Times(0);
  EXPECT_CALL(mock_vrf_table_, set(_)).Times(0);

  EXPECT_OK(ModifyVrfEntryAndUpdateState(mock_vrf_table_, mock_vrf_notifier_,
                                         mock_app_db_client_,
                                         mock_state_db_client_, app_db_values,
                                         table_entry, vrf_id_reference_count_));
  EXPECT_TRUE(vrf_id_reference_count_.empty());
}

TEST_F(VrfEntryTranslationTest, ModifyDoesNotChangeVrfId) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-1" }
                                                 }
                                               })pb",
                                          &table_entry));

  // Assume the current AppDb entry already has the same VRF ID.
  std::unordered_map<std::string, std::string> app_db_values = {
      {"vrf_id", "vrf-1"}};
  vrf_id_reference_count_["vrf-1"] = 1;

  EXPECT_CALL(mock_vrf_table_, del(_)).Times(0);
  EXPECT_CALL(mock_vrf_table_, set(_)).Times(0);
  EXPECT_OK(ModifyVrfEntryAndUpdateState(mock_vrf_table_, mock_vrf_notifier_,
                                         mock_app_db_client_,
                                         mock_state_db_client_, app_db_values,
                                         table_entry, vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, ModifyChangesVrfId) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-1" }
                                                 }
                                               })pb",
                                          &table_entry));

  // Assume the current AppDbEntry has a different VRF ID.
  std::unordered_map<std::string, std::string> app_db_values = {
      {"vrf_id", "vrf-2"}};
  vrf_id_reference_count_["vrf-2"] = 1;

  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-1"), _, _, _)).Times(1);

  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)))
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_SUCCESS"),
                      SetArgReferee<1>("vrf-2"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "Ok")})),
                      Return(true)));

  EXPECT_OK(ModifyVrfEntryAndUpdateState(mock_vrf_table_, mock_vrf_notifier_,
                                         mock_app_db_client_,
                                         mock_state_db_client_, app_db_values,
                                         table_entry, vrf_id_reference_count_));

  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-2"), _, _)).Times(1);
  EXPECT_OK(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                               mock_app_db_client_, mock_state_db_client_,
                               vrf_id_reference_count_));
}

TEST_F(VrfEntryTranslationTest, ModifyChangesVrfIdFailsResp) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(action {
                                                 params {
                                                   name: "vrf_id"
                                                   value { str: "vrf-1" }
                                                 }
                                               })pb",
                                          &table_entry));

  // Assume the current AppDbEntry has a different VRF ID.
  std::unordered_map<std::string, std::string> app_db_values = {
      {"vrf_id", "vrf-2"}};
  vrf_id_reference_count_["vrf-2"] = 1;

  EXPECT_CALL(mock_vrf_table_, set(Eq("vrf-1"), _, _, _)).Times(1);

  // Mock empty return from state db to signify the entry doesn't exist before.
  EXPECT_CALL(mock_state_db_client_, hgetall("VRF_TABLE:vrf-1"))
      .Times(1)
      .WillRepeatedly(Return(std::unordered_map<std::string, std::string>()));
  EXPECT_CALL(mock_app_db_client_, del("VRF_TABLE:vrf-1"))
      .WillOnce(Return(/*value=*/1));

  // Fake an error in the Orchagent response for the new vrf addition.
  EXPECT_CALL(mock_vrf_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("SWSS_RC_INTERNAL"),
                      SetArgReferee<1>("vrf-1"),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("err_str", "my error")})),
                      Return(true)));

  EXPECT_THAT(ModifyVrfEntryAndUpdateState(
                  mock_vrf_table_, mock_vrf_notifier_, mock_app_db_client_,
                  mock_state_db_client_, app_db_values, table_entry,
                  vrf_id_reference_count_),
              StatusIs(absl::StatusCode::kInternal, HasSubstr("my error")));

  // Original vrf-2 should not get deleted.
  EXPECT_CALL(mock_vrf_table_, del(Eq("vrf-2"), _, _)).Times(0);

  EXPECT_OK(PruneVrfReferences(mock_vrf_table_, mock_vrf_notifier_,
                               mock_app_db_client_, mock_state_db_client_,
                               vrf_id_reference_count_));
  // Original vrf-2 reference count should still be 1.
  EXPECT_EQ(vrf_id_reference_count_["vrf-2"], 1);
}

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

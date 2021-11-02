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
#include "p4rt_app/sonic/state_verification.h"

#include <unordered_map>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "swss/mocks/mock_db_connector.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::testing::ElementsAre;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Return;

using ListOfKeys = std::vector<std::string>;
using MapOfValues = std::unordered_map<std::string, std::string>;

TEST(StateVerificationTest, VerifyStateMatches) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read 2 keys from the AppDb and AppStateDb. Order should not matter.
  EXPECT_CALL(mock_app_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key1", "P4RT:key0"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0", "P4RT:key1"}));

  // Because the key0 matches we'll read the full entry from both DBs.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key0"))
      .WillOnce(
          Return(MapOfValues{{"field1", "value1"}, {"field0", "value0"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key0"))
      .WillOnce(
          Return(MapOfValues{{"field0", "value0"}, {"field1", "value1"}}));

  // Because the key1 matches we'll read the full entry from both DBs.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field11", "value11"}, {"field10", "value10"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field10", "value10"}, {"field11", "value11"}}));

  // Because everything matches the state verification should return no errors.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      IsEmpty());
}

TEST(StateVerificationTest, MissingEntryInAppDbFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read only 1 key from the AppDb and 2 keys from the AppStateDb.
  EXPECT_CALL(mock_app_db, keys).WillOnce(Return(ListOfKeys{"P4RT:key1"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0", "P4RT:key1"}));

  // Because the key1 matches we'll read the full entry from both DBs.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field1", "value1"}, {"field0", "value0"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field0", "value0"}, {"field1", "value1"}}));

  // Because of the missing key we should return 1 failure.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("AppDb is missing key")));
}

TEST(StateVerificationTest, MissingEntryInAppStateDbFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read only 2 key from the AppDb and 1 key from the AppStateDb.
  EXPECT_CALL(mock_app_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0", "P4RT:key1"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key1"}));

  // Because the key1 matches we'll read the full entry from both DBs.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field1", "value1"}, {"field0", "value0"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key1"))
      .WillOnce(
          Return(MapOfValues{{"field0", "value0"}, {"field1", "value1"}}));

  // Because of the missing key we should return 1 failure.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("AppStateDb is missing key")));
}

TEST(StateVerificationTest, MissingFieldInAppDbEntryFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read the same key from the AppDb and AppStateDb.
  EXPECT_CALL(mock_app_db, keys).WillOnce(Return(ListOfKeys{"P4RT:key0"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0"}));

  // However, the AppDb entry has 1 less field value.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field1", "value1"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key0"))
      .WillOnce(
          Return(MapOfValues{{"field0", "value0"}, {"field1", "value1"}}));

  // Because of the missing field we should return 1 failure.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("do not match")));
}

TEST(StateVerificationTest, ExtraFieldInAppDbEntryFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read the same key from the AppDb and AppStateDb.
  EXPECT_CALL(mock_app_db, keys).WillOnce(Return(ListOfKeys{"P4RT:key0"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0"}));

  // However, the AppDb entry has 1 more field value.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key0"))
      .WillOnce(
          Return(MapOfValues{{"field0", "value0"}, {"field1", "value1"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field1", "value1"}}));

  // Because of the extra field we should return 1 failure.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("do not match")));
}

TEST(StateVerificationTest, MismatchFieldInEntryFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read the same key from the AppDb and AppStateDb.
  EXPECT_CALL(mock_app_db, keys).WillOnce(Return(ListOfKeys{"P4RT:key0"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0"}));

  // However, the entries have different fields.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field0", "value"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field1", "value"}}));

  // Because of the mismatched field names we should return 1 failure
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("do not match")));
}

TEST(StateVerificationTest, DifferentFieldValuesInEntryFails) {
  swss::MockDBConnector mock_app_state_db;
  swss::MockDBConnector mock_app_db;

  // Read the same key from the AppDb and AppStateDb.
  EXPECT_CALL(mock_app_db, keys).WillOnce(Return(ListOfKeys{"P4RT:key0"}));
  EXPECT_CALL(mock_app_state_db, keys)
      .WillOnce(Return(ListOfKeys{"P4RT:key0"}));

  // However, the entries have different values.
  EXPECT_CALL(mock_app_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field", "value0"}}));
  EXPECT_CALL(mock_app_state_db, hgetall("P4RT:key0"))
      .WillOnce(Return(MapOfValues{{"field", "value1"}}));

  // Because of the differing field values we should return 1 failure.
  EXPECT_THAT(
      VerifyAppStateDbAndAppDbEntries("P4RT", mock_app_state_db, mock_app_db),
      ElementsAre(HasSubstr("do not match")));
}

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

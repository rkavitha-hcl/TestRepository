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
#include "p4rt_app/event_monitoring/port_change_events.h"

#include <deque>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/event_monitoring/mock_state_event_monitor.h"
#include "swss/rediscommand.h"
#include "swss/table.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::Return;

// Expected SONiC commands assumed by PortChangeEvents.
constexpr char kSetCommand[] = "SET";
constexpr char kDelCommand[] = "DEL";

// Helper method to format a SONiC event.
swss::KeyOpFieldsValuesTuple SonicEvent(
    const std::string& op, const std::string& key,
    const std::vector<swss::FieldValueTuple>& field_values) {
  swss::KeyOpFieldsValuesTuple result;
  kfvOp(result) = op;
  kfvKey(result) = key;
  kfvFieldsValues(result) = field_values;
  return result;
}

TEST(PortChangeEventsTest, SetPortEvent) {
  sonic::MockStateEventMonitor mock_state_events;
  PortChangeEvents port_change_events(mock_state_events);

  EXPECT_CALL(mock_state_events, GetNextEvents)
      .WillOnce(Return(std::deque<swss::KeyOpFieldsValuesTuple>{
          SonicEvent(kSetCommand, "eth0", {{"id", "1"}, {"status", "up"}}),
          SonicEvent(kSetCommand, "eth1", {{"id", "4"}, {"status", "down"}}),
      }));

  EXPECT_OK(port_change_events.WaitForEventAndUpdateP4Runtime());
}

TEST(PortChangeEventsTest, SetPortEventMissingIdField) {
  sonic::MockStateEventMonitor mock_state_events;
  PortChangeEvents port_change_events(mock_state_events);

  EXPECT_CALL(mock_state_events, GetNextEvents)
      .WillOnce(Return(std::deque<swss::KeyOpFieldsValuesTuple>{
          SonicEvent(kSetCommand, "eth0", {{"status", "up"}}),
          SonicEvent(kSetCommand, "eth1", {{"status", "down"}}),
      }));

  EXPECT_OK(port_change_events.WaitForEventAndUpdateP4Runtime());
}

TEST(PortChangeEventsTest, DelPortEvent) {
  sonic::MockStateEventMonitor mock_state_events;
  PortChangeEvents port_change_events(mock_state_events);

  EXPECT_CALL(mock_state_events, GetNextEvents)
      .WillOnce(Return(std::deque<swss::KeyOpFieldsValuesTuple>{
          SonicEvent(kDelCommand, "eth0", {{"id", "1"}, {"status", "up"}}),
          SonicEvent(kDelCommand, "eth1", {{"id", "4"}, {"status", "down"}}),
      }));

  EXPECT_OK(port_change_events.WaitForEventAndUpdateP4Runtime());
}

TEST(PortChangeEventsTest, PortEventFailsWithUnknownError) {
  sonic::MockStateEventMonitor mock_state_events;
  PortChangeEvents port_change_events(mock_state_events);

  EXPECT_CALL(mock_state_events, GetNextEvents)
      .WillOnce(Return(absl::UnknownError("my error")));

  EXPECT_THAT(port_change_events.WaitForEventAndUpdateP4Runtime(),
              StatusIs(absl::StatusCode::kUnknown));
}

TEST(PortChangeEventsTest, PortEventFailsWithTimeoutError) {
  sonic::MockStateEventMonitor mock_state_events;
  PortChangeEvents port_change_events(mock_state_events);

  EXPECT_CALL(mock_state_events, GetNextEvents)
      .WillOnce(Return(absl::DeadlineExceededError("my error")));

  EXPECT_THAT(port_change_events.WaitForEventAndUpdateP4Runtime(),
              StatusIs(absl::StatusCode::kDeadlineExceeded));
}

}  // namespace
}  // namespace p4rt_app

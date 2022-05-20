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
#include "p4rt_app/event_monitoring/app_state_db_port_table_event.h"

#include <deque>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/p4runtime/mock_p4runtime_impl.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::HasSubstr;
using ::testing::Return;

// Expected SONiC commands assumed by PortChangeEvents.
constexpr char kSetCommand[] = "SET";
constexpr char kDelCommand[] = "DEL";

TEST(PortChangeEventsTest, SetPortEvent) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  EXPECT_CALL(mock_p4runtime_impl, AddPortTranslation("eth0", "1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_p4runtime_impl, AddPortTranslation("eth1", "4"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(port_change_events.HandleEvent(kSetCommand, "eth0",
                                           {{"id", "1"}, {"status", "up"}}));
  EXPECT_OK(port_change_events.HandleEvent(kSetCommand, "eth1",
                                           {{"id", "4"}, {"status", "down"}}));
}

TEST(PortChangeEventsTest, SetPortEventMissingIdField) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  // Because there is no ID field we remove the port from P4Runtime.
  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation("eth0"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation("eth1"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(
      port_change_events.HandleEvent(kSetCommand, "eth0", {{"status", "up"}}));
  EXPECT_OK(port_change_events.HandleEvent(kSetCommand, "eth1",
                                           {{"status", "down"}}));
}

TEST(PortChangeEventsTest, DelPortEvent) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation("eth0"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation("eth1"))
      .WillOnce(Return(absl::OkStatus()));

  EXPECT_OK(port_change_events.HandleEvent(kDelCommand, "eth0",
                                           {{"id", "1"}, {"status", "up"}}));
  EXPECT_OK(port_change_events.HandleEvent(kDelCommand, "eth1",
                                           {{"id", "4"}, {"status", "down"}}));
}

TEST(PortChangeEventsTest, UnknownPortEvent) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  EXPECT_CALL(mock_p4runtime_impl, AddPortTranslation).Times(0);
  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation).Times(0);

  EXPECT_THAT(port_change_events.HandleEvent(/*op=*/"UNKNOWN", "eth0",
                                             {{"id", "1"}, {"status", "up"}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PortChangeEventsTest, P4RuntimeAddPortFails) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  EXPECT_CALL(mock_p4runtime_impl, AddPortTranslation)
      .WillOnce(Return(absl::InvalidArgumentError("something was bad")));

  EXPECT_THAT(port_change_events.HandleEvent(kSetCommand, "eth0",
                                             {{"id", "1"}, {"status", "up"}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("something was bad")));
}

TEST(PortChangeEventsTest, P4RuntimeRemovePortFails) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation)
      .WillOnce(Return(absl::InvalidArgumentError("something was bad")));

  EXPECT_THAT(port_change_events.HandleEvent(kDelCommand, "eth0",
                                             {{"id", "1"}, {"status", "up"}}),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       HasSubstr("something was bad")));
}

TEST(PortChangeEventsTest, P4RuntimeRemovePortFailsWhenIdIsMissing) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  AppStateDbPortTableEventHandler port_change_events(mock_p4runtime_impl);

  // No ID field means we will try to remove the port.
  EXPECT_CALL(mock_p4runtime_impl, RemovePortTranslation)
      .WillOnce(Return(absl::InvalidArgumentError("something was bad")));

  EXPECT_THAT(
      port_change_events.HandleEvent(kSetCommand, "eth0", {{"status", "up"}}),
      StatusIs(absl::StatusCode::kInvalidArgument,
               HasSubstr("something was bad")));
}

}  // namespace
}  // namespace p4rt_app

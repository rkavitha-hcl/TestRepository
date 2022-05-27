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
#include "p4rt_app/event_monitoring/config_db_port_table_event.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/p4runtime/mock_p4runtime_impl.h"
#include "p4rt_app/sonic/adapters/mock_table_adapter.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;

// Expected SONiC commands assumed by state events.
constexpr char kSetCommand[] = "SET";
constexpr char kDelCommand[] = "DEL";

std::vector<std::pair<std::string, std::string>> IdValueEntry(
    const std::string& id) {
  return {{"id", id}};
}

TEST(PortTableIdEventTest, SetPortId) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  EXPECT_CALL(*mock_app_db, set("Ethernet1/1/1", IdValueEntry("1"))).Times(1);
  EXPECT_CALL(*mock_app_state_db, set("Ethernet1/1/1", IdValueEntry("1")))
      .Times(1);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_OK(
      event_handler.HandleEvent(kSetCommand, "Ethernet1/1/1", {{"id", "1"}}));
}

TEST(PortTableIdEventTest, UpdatePortId) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  EXPECT_CALL(*mock_app_db, set("Ethernet1/1/1", IdValueEntry("2"))).Times(1);
  EXPECT_CALL(*mock_app_db, set("Ethernet1/1/1", IdValueEntry("3"))).Times(1);
  EXPECT_CALL(*mock_app_state_db, set("Ethernet1/1/1", IdValueEntry("2")))
      .Times(1);
  EXPECT_CALL(*mock_app_state_db, set("Ethernet1/1/1", IdValueEntry("3")))
      .Times(1);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_OK(
      event_handler.HandleEvent(kSetCommand, "Ethernet1/1/1", {{"id", "2"}}));
  EXPECT_OK(
      event_handler.HandleEvent(kSetCommand, "Ethernet1/1/1", {{"id", "3"}}));
}

TEST(PortTableIdEventTest, SetPortIdToAnEmptyString) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  EXPECT_CALL(*mock_app_db, del("Ethernet1/1/1")).Times(1);
  EXPECT_CALL(*mock_app_state_db, del("Ethernet1/1/1")).Times(1);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_OK(
      event_handler.HandleEvent(kSetCommand, "Ethernet1/1/1", {{"id", ""}}));
}

TEST(PortTableIdEventTest, DeletePortId) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  EXPECT_CALL(*mock_app_db, del("Ethernet1/1/1")).Times(1);
  EXPECT_CALL(*mock_app_state_db, del("Ethernet1/1/1")).Times(1);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_OK(
      event_handler.HandleEvent(kDelCommand, "Ethernet1/1/1", {{"id", "1"}}));
}

TEST(PortTableIdEventTest, NonEthernetPortIsIgnored) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  EXPECT_CALL(*mock_app_db, set).Times(0);
  EXPECT_CALL(*mock_app_db, del).Times(0);
  EXPECT_CALL(*mock_app_state_db, set).Times(0);
  EXPECT_CALL(*mock_app_state_db, del).Times(0);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_OK(event_handler.HandleEvent(kDelCommand, "loopback0", {{"id", "1"}}));
}

TEST(PortTableIdEventTest, UnexpectedOperationReturnsAnError) {
  MockP4RuntimeImpl mock_p4runtime_impl;
  auto mock_app_db = std::make_unique<sonic::MockTableAdapter>();
  auto mock_app_state_db = std::make_unique<sonic::MockTableAdapter>();

  // Invalid operations should not update any redis state.
  EXPECT_CALL(*mock_app_db, set).Times(0);
  EXPECT_CALL(*mock_app_db, del).Times(0);
  EXPECT_CALL(*mock_app_state_db, set).Times(0);
  EXPECT_CALL(*mock_app_state_db, del).Times(0);

  ConfigDbPortTableEventHandler event_handler(&mock_p4runtime_impl,
                                              std::move(mock_app_db),
                                              std::move(mock_app_state_db));
  EXPECT_THAT(event_handler.HandleEvent("INVALID_OPERATION", "Ethernet1/1/1",
                                        {{"id", "1"}}),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace p4rt_app

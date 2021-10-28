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
#include "p4rt_app/event_monitoring/state_verification_events.h"

#include <vector>

#include "absl/status/status.h"
#include "absl/synchronization/notification.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "swss/mocks/mock_consumer_notifier.h"
#include "swss/mocks/mock_db_connector.h"
#include "swss/rediscommand.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::SetArgReferee;

class StateVerificationEventsTest : public testing::Test {
 protected:
  StateVerificationEventsTest()
      : state_verification_(StateVerificationEvents(mock_consumer_notifier_,
                                                    mock_db_connector_)) {}

  StateVerificationEvents state_verification_;
  swss::MockConsumerNotifier mock_consumer_notifier_;
  swss::MockDBConnector mock_db_connector_;
};

TEST_F(StateVerificationEventsTest, GetEventAndUpdateRedisDbState) {
  EXPECT_CALL(mock_consumer_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("p4rt:p4rt"),
                      SetArgReferee<1>("sample_timestamp"), Return(true)));
  EXPECT_CALL(mock_db_connector_, hmset("VERIFY_STATE_RESP_TABLE|p4rt:p4rt",
                                        std::vector<swss::FieldValueTuple>{
                                            {"timestamp", "sample_timestamp"},
                                            {"status", "pass"},
                                            {"err_str", ""},
                                        }))
      .Times(1);

  EXPECT_OK(state_verification_.WaitForEventAndVerifyP4Runtime());
}

TEST_F(StateVerificationEventsTest, DoNotUpdateStateWhenNotificationFails) {
  EXPECT_CALL(mock_consumer_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("p4rt:p4rt"),
                      SetArgReferee<1>("sample_timestamp"), Return(false)));
  EXPECT_CALL(mock_db_connector_, hmset).Times(0);

  EXPECT_THAT(state_verification_.WaitForEventAndVerifyP4Runtime(),
              StatusIs(absl::StatusCode::kUnknown));
}

TEST_F(StateVerificationEventsTest,
       DoNotUpdateStateWhenNotificationIsForDifferentComponent) {
  // Instead of getting a notification for p4rt, we get a notification for swss.
  EXPECT_CALL(mock_consumer_notifier_, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>("swss:swss"),
                      SetArgReferee<1>("sample_timestamp"), Return(true)));
  EXPECT_CALL(mock_db_connector_, hmset).Times(0);

  EXPECT_OK(state_verification_.WaitForEventAndVerifyP4Runtime());
}

TEST_F(StateVerificationEventsTest, EventThreadStartStop) {
  // This test will spawn a thread inside the StateVerificationEvents object
  // which is stopped when the object is destroyed. To ensure we run through the
  // loop at least once we block on a notification.
  absl::Notification was_run;

  // We expect the wait call be be made at least once, but could be called again
  // while waiting for the thread to be stopped.
  EXPECT_CALL(mock_consumer_notifier_, WaitForNotificationAndPop)
      .WillOnce([&was_run](std::string& op, std::string& key,
                           std::vector<swss::FieldValueTuple>& fv,
                           int64_t timeout) {
        was_run.Notify();
        return false;
      })
      .WillRepeatedly(Return(false));

  // Spawn the thread, and wait for it to do work before finishing the test.
  state_verification_.Start();
  was_run.WaitForNotification();
  state_verification_.Stop();
}

}  // namespace
}  // namespace p4rt_app

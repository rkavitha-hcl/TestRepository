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
#include "p4rt_app/sonic/response_handler.h"

#include <memory>

#include "absl/status/status.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/ir.h"
#include "swss/mocks/mock_consumer_notifier.h"
#include "swss/mocks/mock_db_connector.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::google::rpc::Code;

// List of keys used in the test.
static constexpr absl::string_view kSampleKey1 =
    R"(P4RT:FIXED_ROUTER_INTERFACE_TABLE:)"
    R"({"priority":123,"match/router_interface_id":"1"})";
static constexpr absl::string_view kSampleKey2 =
    R"(P4RT:FIXED_ROUTER_INTERFACE_TABLE:)"
    R"({"priority":123,"match/router_interface_id":"2"})";

// Swss string to indicate status of the transaction, these are coming from
// sonic-swss-common/common/status_code_util.h.
static constexpr absl::string_view kSwssSuccess = "SWSS_RC_SUCCESS";
static constexpr absl::string_view kSwssRcInternal = "SWSS_RC_INTERNAL";

// Expected first part of tuple value in the response notification - 'err_str'.
static constexpr absl::string_view kErrorString = "err_str";

using ::gutil::StatusIs;
using ::testing::DoAll;
using ::testing::Eq;
using ::testing::HasSubstr;
using ::testing::InSequence;
using ::testing::Return;
using ::testing::SetArgReferee;
using ::testing::Test;

// Setup the mocks for WaitForNotificationAndPop call.
// Takes as input the vector of keys, expected Orchagent return codes and the
// bool return value.
// Returns the mocked notifier object.
std::unique_ptr<swss::MockConsumerNotifier> SetupMockForConsumerNotifier(
    absl::Span<const std::string> keys,
    absl::Span<const std::string> swss_status, bool return_value) {
  auto mock_notifier = absl::make_unique<swss::MockConsumerNotifier>();
  InSequence seq;
  for (uint32_t i = 0; i < keys.size(); i++) {
    // Strip the table name prefix as the notification response does not have
    // it.
    auto response_key = std::string(keys[i].substr(keys[i].find(":") + 1));
    EXPECT_CALL(*mock_notifier, WaitForNotificationAndPop)
        .WillOnce(DoAll(SetArgReferee<0>(swss_status[i]),
                        SetArgReferee<1>(response_key),
                        SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                            {swss::FieldValueTuple(kErrorString, "Ok")})),
                        Return(return_value)));
  }
  return mock_notifier;
}

TEST(ResponseHandlerTest, GetAppDbResponsesOk) {
  const std::vector<std::string> keys = {{std::string(kSampleKey1)},
                                         {std::string(kSampleKey2)}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)},
                                                {std::string(kSwssSuccess)}};
  // Setup the expected responses (success) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, true);
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteRpcStatus ir_rpc_status;
  pdpi::IrWriteResponse* ir_write_response =
      ir_rpc_status.mutable_rpc_response();

  EXPECT_OK(GetAndProcessResponseNotification(
      keys, keys.size(), *mock_notifier, mock_app_db_client,
      mock_state_db_client, *ir_write_response));
  EXPECT_OK(pdpi::IrWriteRpcStatusToGrpcStatus(ir_rpc_status));
}

TEST(ResponseHandlerTest, GetAppDbResponsesPopError) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)}};

  // Setup the expected responses (internal error) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, false);
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteResponse ir_write_response;
  EXPECT_THAT(GetAndProcessResponseNotification(
                  keys, keys.size(), *mock_notifier, mock_app_db_client,
                  mock_state_db_client, ir_write_response),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Timeout or other errors")));
}

TEST(ResponseHandlerTest, GetAppDbResponsesEmptyTuple) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}}};
  auto mock_notifier = absl::make_unique<swss::MockConsumerNotifier>();
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // No response tuple(arg 2 in the pop call) returned in the response.
  EXPECT_CALL(*mock_notifier, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>(std::string(kSwssSuccess)),
                      SetArgReferee<1>(keys[0]), Return(true)));

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteResponse ir_write_response;

  EXPECT_THAT(
      GetAndProcessResponseNotification(
          keys, keys.size(), *mock_notifier, mock_app_db_client,
          mock_state_db_client, ir_write_response),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("should not be empty")));
}

TEST(ResponseHandlerTest, GetAppDbResponsesBadErrorString) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}}};
  auto mock_notifier = absl::make_unique<swss::MockConsumerNotifier>();
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // Not 'err_str' in the pop call that is returned in the response.
  EXPECT_CALL(*mock_notifier, WaitForNotificationAndPop)
      .WillOnce(DoAll(SetArgReferee<0>(std::string(kSwssSuccess)),
                      SetArgReferee<1>(keys[0]),
                      SetArgReferee<2>(std::vector<swss::FieldValueTuple>(
                          {swss::FieldValueTuple("not_err_str", "Success")})),
                      Return(true)));

  // Because the GetAndProcessResponse returns an unexpected response string we
  // returng an INTERNAL error.
  pdpi::IrWriteResponse ir_write_response;
  EXPECT_THAT(GetAndProcessResponseNotification(
                  keys, keys.size(), *mock_notifier, mock_app_db_client,
                  mock_state_db_client, ir_write_response),
              StatusIs(absl::StatusCode::kInternal));
}

// TODO (b/173436594)
TEST(ResponseHandlerTest, DISABLED_GetAppDbResponsesDuplicateKey) {
  // Repeat the same key in the responses.
  const std::vector<std::string> keys = {{std::string{kSampleKey1}},
                                         {std::string{kSampleKey1}}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)},
                                                {std::string(kSwssSuccess)}};

  // Setup the expected responses (success) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, true);
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteRpcStatus ir_rpc_status;
  pdpi::IrWriteResponse* ir_write_response =
      ir_rpc_status.mutable_rpc_response();

  EXPECT_THAT(GetAndProcessResponseNotification(
                  keys, keys.size(), *mock_notifier, mock_app_db_client,
                  mock_state_db_client, *ir_write_response),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("serveral keys with the same name")));
  EXPECT_THAT(pdpi::IrWriteRpcStatusToGrpcStatus(ir_rpc_status),
              StatusIs(absl::StatusCode::kUnknown));

  // Expect the code value to be set as INTERNAL error for one entry.
  EXPECT_EQ(ir_write_response->statuses(1).code(), Code::INTERNAL);
}

TEST(ResponseHandlerTest, RestoreAppDbModifyOk) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}},
                                         {std::string{kSampleKey2}}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)},
                                                {std::string(kSwssRcInternal)}};
  std::unordered_map<std::string, std::string> app_db_values = {
      {"action", "set_port_and_src_mac"},
      {"param/port", "Ethernet28/5"},
      {"param/src_mac", "00:02:03:04:05:06"},
  };

  // Setup the expected responses (success) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, true);

  swss::MockDBConnector mock_statedb_client;
  EXPECT_CALL(mock_statedb_client, hgetall(Eq(kSampleKey2)))
      .WillRepeatedly(Return(app_db_values));
  swss::MockDBConnector mock_appdb_client;
  EXPECT_CALL(mock_appdb_client, hmset).Times(1);

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteResponse ir_write_response;
  EXPECT_OK(GetAndProcessResponseNotification(
      keys, keys.size(), *mock_notifier, mock_appdb_client, mock_statedb_client,
      ir_write_response));
  // Expect the code value to be INTERNAL for the second key.
  EXPECT_EQ(ir_write_response.statuses(0).code(), Code::OK);
  EXPECT_EQ(ir_write_response.statuses(1).code(), Code::INTERNAL);
}

TEST(ResponseHandlerTest, RestoreAppDbDelOk) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}},
                                         {std::string{kSampleKey2}}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)},
                                                {std::string(kSwssRcInternal)}};

  // Setup the expected responses (success) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, true);

  swss::MockDBConnector mock_statedb_client;
  // Return empty map to reflect that the entry does not exist in
  // APPL_STATE_DB.
  EXPECT_CALL(mock_statedb_client, hgetall(Eq(kSampleKey2)))
      .WillOnce(Return(std::unordered_map<std::string, std::string>{}));

  swss::MockDBConnector mock_appdb_client;
  EXPECT_CALL(mock_appdb_client, del(std::string(kSampleKey2)))
      .Times(1)
      .WillOnce(Return(1));

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteResponse ir_write_response;
  EXPECT_OK(GetAndProcessResponseNotification(
      keys, keys.size(), *mock_notifier, mock_appdb_client, mock_statedb_client,
      ir_write_response));
  // Expect the code value to be INTERNAL for the second key.
  EXPECT_EQ(ir_write_response.statuses(0).code(), Code::OK);
  EXPECT_EQ(ir_write_response.statuses(1).code(), Code::INTERNAL);
}

TEST(ResponseHandlerTest, RestoreAppDbDelError) {
  const std::vector<std::string> keys = {{std::string{kSampleKey1}},
                                         {std::string{kSampleKey2}}};
  const std::vector<std::string> swss_status = {{std::string(kSwssSuccess)},
                                                {std::string(kSwssRcInternal)}};

  // Setup the expected responses (success) from mock.
  auto mock_notifier = SetupMockForConsumerNotifier(keys, swss_status, true);

  swss::MockDBConnector mock_statedb_client;
  // Return empty map to reflect that the entry does not exist in
  // APPL_STATE_DB.
  EXPECT_CALL(mock_statedb_client, hgetall(Eq(kSampleKey2)))
      .WillOnce(Return(std::unordered_map<std::string, std::string>{}));

  swss::MockDBConnector mock_appdb_client;
  // Return 0 for number of entries deleted.
  EXPECT_CALL(mock_appdb_client, del(std::string(kSampleKey2)))
      .Times(1)
      .WillOnce(Return(0));

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteResponse ir_write_response;
  EXPECT_THAT(GetAndProcessResponseNotification(
                  keys, keys.size(), *mock_notifier, mock_appdb_client,
                  mock_statedb_client, ir_write_response),
              StatusIs(absl::StatusCode::kInternal,
                       HasSubstr("Unexpected number of delete entries")));
  // Expect the code value to be INTERNAL for the second key.
  EXPECT_EQ(ir_write_response.statuses(0).code(), Code::OK);
  EXPECT_EQ(ir_write_response.statuses(1).code(), Code::INTERNAL);
}

TEST(ResponseHandlerTest, GetAndProcessRespWriteRespSizeMismatch) {
  swss::MockConsumerNotifier mock_notifier;
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;
  const std::vector<std::string> keys = {{std::string{kSampleKey1}}};
  // Add 2 responses instead of the expected 1 response.
  pdpi::IrWriteResponse ir_write_response;
  ir_write_response.add_statuses();
  ir_write_response.add_statuses();
  EXPECT_THAT(GetAndProcessResponseNotification(
                  keys, keys.size(), mock_notifier, mock_app_db_client,
                  mock_state_db_client, ir_write_response),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

struct SwssToP4rtErrorMapping {
  std::string test_name;
  std::string swss_error;
  Code p4rt_error;
};

using ResponseHandleParamTest =
    ::testing::TestWithParam<SwssToP4rtErrorMapping>;

TEST_P(ResponseHandleParamTest, VerifyAllErrors) {
  const std::vector<std::string> keys = {std::string(kSampleKey1)};
  const SwssToP4rtErrorMapping& error = GetParam();
  const std::vector<std::string> swss_status = {error.swss_error};

  // Setup the expected responses from mock.
  auto mock_notifier =
      SetupMockForConsumerNotifier(keys, swss_status, /*return_value*/ true);
  swss::MockDBConnector mock_app_db_client;
  swss::MockDBConnector mock_state_db_client;

  // Failed response error code will try to restore the APP_DB, since this is an
  // insert failure, fake a successful delete.
  EXPECT_CALL(mock_app_db_client, del(std::string(kSampleKey1)))
      .WillOnce(Return(1));

  // Invoke the GetAndProcessResponse call.
  pdpi::IrWriteRpcStatus ir_rpc_status;
  pdpi::IrWriteResponse* ir_write_response =
      ir_rpc_status.mutable_rpc_response();

  EXPECT_OK(GetAndProcessResponseNotification(
      keys, keys.size(), *mock_notifier, mock_app_db_client,
      mock_state_db_client, *ir_write_response));
  EXPECT_EQ(ir_write_response->statuses(0).code(), error.p4rt_error);
}

INSTANTIATE_TEST_SUITE_P(
    ResponseHandleErrorTest, ResponseHandleParamTest,
    ::testing::ValuesIn<SwssToP4rtErrorMapping>({
        {.test_name = "InvalidParam",
         .swss_error = "SWSS_RC_INVALID_PARAM",
         .p4rt_error = Code::INVALID_ARGUMENT},
        {.test_name = "DeadlineExceeded",
         .swss_error = "SWSS_RC_DEADLINE_EXCEEDED",
         .p4rt_error = Code::DEADLINE_EXCEEDED},
        {.test_name = "Unavailable",
         .swss_error = "SWSS_RC_UNAVAIL",
         .p4rt_error = Code::UNAVAILABLE},
        {.test_name = "NotFound",
         .swss_error = "SWSS_RC_NOT_FOUND",
         .p4rt_error = Code::NOT_FOUND},
        {.test_name = "NoMemory",
         .swss_error = "SWSS_RC_NO_MEMORY",
         .p4rt_error = Code::INTERNAL},
        {.test_name = "PermDenied",
         .swss_error = "SWSS_RC_PERMISSION_DENIED",
         .p4rt_error = Code::PERMISSION_DENIED},
        {.test_name = "Full",
         .swss_error = "SWSS_RC_FULL",
         .p4rt_error = Code::RESOURCE_EXHAUSTED},
        {.test_name = "InUse",
         .swss_error = "SWSS_RC_IN_USE",
         .p4rt_error = Code::INVALID_ARGUMENT},
        {.test_name = "Internal",
         .swss_error = "SWSS_RC_INTERNAL",
         .p4rt_error = Code::INTERNAL},
        {.test_name = "Unknown",
         .swss_error = "SWSS_RC_UNKNOWN",
         .p4rt_error = Code::UNKNOWN},
        {.test_name = "UnImplemented",
         .swss_error = "SWSS_RC_UNIMPLEMENTED",
         .p4rt_error = Code::UNIMPLEMENTED},
    }),
    [](const testing::TestParamInfo<ResponseHandleParamTest::ParamType>& info) {
      return info.param.test_name;
    });

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

#include "tests/forwarding/master_arbitration_test.h"

#include "absl/numeric/int128.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "google/rpc/code.pb.h"
#include "grpcpp/grpcpp.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "sai_p4/fixed/roles.h"
#include "tests/forwarding/p4_blackbox_fixture.h"
#include "thinkit/test_environment.h"

namespace gpins {
namespace {

using ::google::rpc::ALREADY_EXISTS;
using ::grpc::ClientContext;
using ::p4::v1::ReadRequest;
using ::p4::v1::ReadResponse;
using ::p4::v1::StreamMessageResponse;
using ::p4::v1::WriteRequest;
using ::testing::Matcher;
using ::testing::Not;

constexpr char kWriteRequest[] = R"(
    updates {
      type: INSERT
      entity {
        # Adding an entry into the router_interface_table (table_id = 33554497).
        table_entry {
          table_id: 33554497
          match {
            field_id: 1
            exact {
              value: "router-interface-4"
            }
          }
          action {
            action {
              action_id: 16777218
              params {
                param_id: 1
                value: "7"
              }
              params {
                param_id: 2
                value: "\002*\020\000\000\003"
              }
            }
          }
        }
      }
    })";

p4::v1::Uint128 CreateElectionId(absl::uint128 election_id) {
  p4::v1::Uint128 id;
  id.set_low(absl::Uint128Low64(election_id));
  id.set_high(absl::Uint128High64(election_id));
  return id;
}

// Generates a write request that inserts a new entry into the
// router_interface_table with the last byte of router_interface_id set to num
WriteRequest GetWriteRequest(int num, absl::uint128 election_id,
                             uint32_t device_id) {
  WriteRequest request = gutil::ParseProtoOrDie<WriteRequest>(kWriteRequest);
  for (auto& update : *request.mutable_updates()) {
    std::string value;
    for (auto& match :
         *(update.mutable_entity()->mutable_table_entry()->mutable_match())) {
      std::string new_value = match.exact().value();
      new_value.back() = num & 0xFF;
      match.mutable_exact()->set_value(new_value);
    }
  }
  request.set_device_id(device_id);
  request.set_role(P4RUNTIME_ROLE_SDN_CONTROLLER);
  *request.mutable_election_id() = CreateElectionId(election_id);
  return request;
}

absl::Status SendStreamMessageRequest(
    P4Runtime::Stub* stub, const p4::v1::StreamMessageRequest& request) {
  grpc::ClientContext context;
  return stub->StreamChannel(&context)->Write(request)
             ? absl::OkStatus()
             : gutil::InternalErrorBuilder()
                   << "Failed to write stream message request: "
                   << request.DebugString();
}

// Returns a matcher that checks if the attempt to become master was successful.
testing::Matcher<absl::Status> NotMaster() { return Not(gutil::IsOk()); }

TEST_P(MasterArbitrationTestFixture, BecomeMaster) {
  TestEnvironment().SetTestCaseID("c6506d76-5041-4f69-b398-a808ab473186");
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0));
}

TEST_P(MasterArbitrationTestFixture, FailToBecomeMaster) {
  TestEnvironment().SetTestCaseID("60c56f72-96ca-4aea-8cdc-16e1b928d53a");
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));
  ASSERT_THAT(BecomeMaster(0).status(), NotMaster());
}

TEST_P(MasterArbitrationTestFixture, ReplaceMaster) {
  TestEnvironment().SetTestCaseID("03da98ad-c4c7-443f-bcc0-53f97103d0c3");
  ASSERT_OK_AND_ASSIGN(auto connection1, BecomeMaster(1));
  ASSERT_OK_AND_ASSIGN(auto connection2, BecomeMaster(2));
}

TEST_P(MasterArbitrationTestFixture, ReplaceMasterAfterFailure) {
  TestEnvironment().SetTestCaseID("d5ffe4cc-ff0e-4d93-8334-a23f06c6232a");
  ASSERT_OK_AND_ASSIGN(auto connection1, BecomeMaster(1));
  ASSERT_THAT(BecomeMaster(0).status(), NotMaster());
  ASSERT_OK_AND_ASSIGN(auto connection2, BecomeMaster(2));
}

TEST_P(MasterArbitrationTestFixture, FailToBecomeMasterAfterMasterDisconnect) {
  TestEnvironment().SetTestCaseID("53b4b886-c218-4c85-b212-13d32105c795");
  {
    ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));
    ASSERT_OK(connection->Finish());
  }
  ASSERT_THAT(BecomeMaster(0).status(), NotMaster());
}

TEST_P(MasterArbitrationTestFixture, ReconnectMaster) {
  TestEnvironment().SetTestCaseID("d95a4da4-139d-4bd6-a43c-dbdefb123fcf");
  {
    ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0));
    ASSERT_OK(connection->Finish());
  }
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0));
}

TEST_P(MasterArbitrationTestFixture, DoubleMaster) {
  TestEnvironment().SetTestCaseID("19614b15-ce8f-4832-9164-342c5585283a");
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0));
  ASSERT_THAT(BecomeMaster(0).status(), NotMaster());
}

TEST_P(MasterArbitrationTestFixture, LongEvolution) {
  TestEnvironment().SetTestCaseID("a65deb93-e350-4322-a932-af699c4b583c");
  {
    ASSERT_OK_AND_ASSIGN(auto connection1, BecomeMaster(1));
    ASSERT_THAT(BecomeMaster(0).status(), NotMaster());
    ASSERT_OK_AND_ASSIGN(auto connection2, BecomeMaster(2));
    ASSERT_THAT(BecomeMaster(1).status(), NotMaster());
    ASSERT_OK_AND_ASSIGN(auto connection3, BecomeMaster(3));
    ASSERT_OK_AND_ASSIGN(auto connection4, BecomeMaster(4));
    {
      ASSERT_OK_AND_ASSIGN(auto connection5, BecomeMaster(5));
      ASSERT_THAT(BecomeMaster(2).status(), NotMaster());
      ASSERT_THAT(BecomeMaster(3).status(), NotMaster());
      ASSERT_THAT(BecomeMaster(4).status(), NotMaster());
      ASSERT_OK(connection5->Finish());
    }
    ASSERT_OK_AND_ASSIGN(auto connection5, BecomeMaster(5));
    ASSERT_OK_AND_ASSIGN(auto connection6, BecomeMaster(6));
    ASSERT_OK_AND_ASSIGN(auto connection7, BecomeMaster(7));
    ASSERT_THAT(BecomeMaster(7).status(), NotMaster());
  }
  ASSERT_THAT(BecomeMaster(1).status(), NotMaster());
  ASSERT_THAT(BecomeMaster(2).status(), NotMaster());
  ASSERT_THAT(BecomeMaster(3).status(), NotMaster());
  ASSERT_THAT(BecomeMaster(4).status(), NotMaster());
  ASSERT_THAT(BecomeMaster(6).status(), NotMaster());
  ASSERT_OK_AND_ASSIGN(auto connection7, BecomeMaster(7));
}

TEST_P(MasterArbitrationTestFixture, SlaveCannotWrite) {
  TestEnvironment().SetTestCaseID("4c714d8-73c6-48b1-ada6-8ac2e5267714");

  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(2));
  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  ASSERT_THAT(BecomeMaster(std::move(stub), 1).status(), NotMaster());
  ASSERT_OK_AND_ASSIGN(auto stub2, Stub());
  ASSERT_FALSE(
      pdpi::SendPiWriteRequest(
          stub2.get(), GetWriteRequest(1, ElectionIdFromLower(1), DeviceId()))
          .ok());
}

TEST_P(MasterArbitrationTestFixture, SlaveCanRead) {
  TestEnvironment().SetTestCaseID("fb678921-d150-4535-b7b8-fc8cecb79a78");

  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));

  // Normalize switch state first when there are write requests involved.
  ASSERT_OK(NormalizeSwitchState(connection.get()));

  ASSERT_OK(pdpi::SendPiWriteRequest(
      &connection->Stub(),
      GetWriteRequest(0, ElectionIdFromLower(1), DeviceId())));

  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  ASSERT_THAT(BecomeMaster(std::move(stub), 0).status(), NotMaster());

  ReadRequest read_everything = gutil::ParseProtoOrDie<ReadRequest>(R"pb(
    entities { table_entry { meter_config {} } }
  )pb");
  read_everything.set_device_id(DeviceId());
  read_everything.set_role(P4RUNTIME_ROLE_SDN_CONTROLLER);
  ::grpc::ClientContext context;
  ASSERT_OK_AND_ASSIGN(auto stub2, Stub());
  std::unique_ptr<::grpc::ClientReader<ReadResponse>> response_stream =
      stub2->Read(&context, read_everything);
  ReadResponse response;
  EXPECT_TRUE(response_stream->Read(&response));
  // The switch should always return some const entries.
  ASSERT_FALSE(response.entities().empty());
  // Clear all table entries to leave the switch in a clean state.
  ASSERT_OK(pdpi::ClearTableEntries(connection.get(), IrP4Info()));
}

TEST_P(MasterArbitrationTestFixture, GetNotifiedOfActualMaster) {
  TestEnvironment().SetTestCaseID("46b83014-759b-4393-bb58-220c0ca38711");
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));

  // Assemble arbitration request.
  p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(DeviceId());
  arbitration->mutable_election_id()->set_high(
      absl::Uint128High64(ElectionIdFromLower(0)));
  arbitration->mutable_election_id()->set_low(
      absl::Uint128Low64(ElectionIdFromLower(0)));
  arbitration->mutable_role()->set_name(P4RUNTIME_ROLE_SDN_CONTROLLER);

  // Send arbitration request.
  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  grpc::ClientContext context;
  auto stream_channel = stub->StreamChannel(&context);
  stream_channel->Write(request);

  // Wait for arbitration response.
  p4::v1::StreamMessageResponse response;
  ASSERT_TRUE(stream_channel->Read(&response));
  EXPECT_EQ(response.update_case(), StreamMessageResponse::kArbitration);
  EXPECT_EQ(response.arbitration().device_id(), DeviceId());
  EXPECT_EQ(response.arbitration().election_id().high(),
            absl::Uint128High64(ElectionIdFromLower(1)));
  EXPECT_EQ(response.arbitration().election_id().low(),
            absl::Uint128Low64(ElectionIdFromLower(1)));
  EXPECT_EQ(response.arbitration().role().name(),
            P4RUNTIME_ROLE_SDN_CONTROLLER);
  EXPECT_EQ(response.arbitration().status().code(), ALREADY_EXISTS);
}

TEST_P(MasterArbitrationTestFixture, NoIdControllerCannotBecomeMaster) {
  TestEnvironment().SetTestCaseID("3699fc43-5ff8-44ee-8965-68f42c71c1ed");

  // Assemble arbitration request.
  p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(DeviceId());
  arbitration->mutable_role()->set_name(P4RUNTIME_ROLE_SDN_CONTROLLER);

  // Send arbitration request.
  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  grpc::ClientContext context;
  auto stream_channel = stub->StreamChannel(&context);
  ASSERT_TRUE(stream_channel->Write(request))
      << "Failed to write stream message request: " << request.DebugString();

  // Wait for arbitration response.
  p4::v1::StreamMessageResponse response;
  ASSERT_TRUE(stream_channel->Read(&response))
      << "Failed to read stream message response: " << response.DebugString();
  EXPECT_EQ(response.update_case(), StreamMessageResponse::kArbitration);
  EXPECT_EQ(response.arbitration().device_id(), DeviceId());
  EXPECT_EQ(response.arbitration().role().name(),
            P4RUNTIME_ROLE_SDN_CONTROLLER);
  // Check that there is no master controller found. In other words, the master
  // arbitration request with no election id failed.
  EXPECT_EQ(response.arbitration().status().code(),
            grpc::StatusCode::NOT_FOUND);
}

TEST_P(MasterArbitrationTestFixture, OldMasterCannotWriteAfterNewMasterCameUp) {
  TestEnvironment().SetTestCaseID("e4bc86a2-84f0-450a-888a-8a6f5f26fa8c");

  int id1 = 1, id2 = 2;
  // Connects controller C1 with id=1 to become master.
  ASSERT_OK_AND_ASSIGN(auto c1, BecomeMaster(id1));

  // Normalize switch state first when there are write requests involved.
  ASSERT_OK(NormalizeSwitchState(c1.get()));

  ASSERT_OK(pdpi::SendPiWriteRequest(
      &c1->Stub(), GetWriteRequest(0, ElectionIdFromLower(id1), DeviceId())));
  ASSERT_OK(pdpi::ClearTableEntries(c1.get(), IrP4Info()));

  // Connects controller C2 with id=2 > 1 to become master.
  ASSERT_OK_AND_ASSIGN(auto c2, BecomeMaster(id2));
  // Checks new master C2 can write.
  ASSERT_OK(pdpi::SendPiWriteRequest(
      &c2->Stub(), GetWriteRequest(1, ElectionIdFromLower(id2), DeviceId())));
  ASSERT_OK(pdpi::ClearTableEntries(c2.get(), IrP4Info()));

  // Checks C1 cannot write after new master C2 came up.
  ASSERT_FALSE(
      pdpi::SendPiWriteRequest(
          &c1->Stub(), GetWriteRequest(2, ElectionIdFromLower(id1), DeviceId()))
          .ok());
}

TEST_P(MasterArbitrationTestFixture, MasterDowngradesItself) {
  TestEnvironment().SetTestCaseID("3cb62c0f-4a1a-430c-978c-a3a2a11078cd");
  int id1 = 1, id2 = 2;

  // Connects controller with id=2 to become master.
  ASSERT_OK_AND_ASSIGN(auto controller, BecomeMaster(id2));

  // Normalize switch state first when there are write requests involved.
  ASSERT_OK(NormalizeSwitchState(controller.get()));

  // Checks new master controller can write.
  ASSERT_OK(pdpi::SendPiWriteRequest(
      &controller->Stub(),
      GetWriteRequest(0, ElectionIdFromLower(id2), DeviceId())));

  ASSERT_OK(pdpi::ClearTableEntries(controller.get(), IrP4Info()));

  // C2 sends master arbitration request with id=1 to downgrade itself.
  p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(DeviceId());
  arbitration->mutable_election_id()->set_high(
      absl::Uint128High64(ElectionIdFromLower(id1)));
  arbitration->mutable_election_id()->set_low(
      absl::Uint128Low64(ElectionIdFromLower(id1)));
  ASSERT_OK(SendStreamMessageRequest(&controller->Stub(), request));

  // Checks C2 cannot write after downgrading.
  ASSERT_FALSE(pdpi::SendPiWriteRequest(
                   &controller->Stub(),
                   GetWriteRequest(1, ElectionIdFromLower(id1), DeviceId()))
                   .ok());
}

}  // namespace
}  // namespace gpins

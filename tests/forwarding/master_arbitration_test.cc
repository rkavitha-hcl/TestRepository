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
          # Match on router_interface_id (32 bits) and sets the egress_spec (the
          # port to send the packet to, param_id=1) and src mac (param_id=2).
          match {
            field_id: 1
            exact { value: "\000\000\000\037" }
          }
          action {
            action {
              action_id: 16777218
              params { param_id: 1 value: "\000\000\0004" }
              params { param_id: 2 value: "\0022\000\000\000\000" }
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
  *request.mutable_election_id() = CreateElectionId(election_id);
  return request;
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
  { ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1)); }
  { ASSERT_THAT(BecomeMaster(0).status(), NotMaster()); }
}

TEST_P(MasterArbitrationTestFixture, ReconnectMaster) {
  TestEnvironment().SetTestCaseID("d95a4da4-139d-4bd6-a43c-dbdefb123fcf");
  { ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0)); }
  { ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(0)); }
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
  auto session = BecomeMaster(std::move(stub), 1);
  ASSERT_THAT(session.status(), NotMaster());

  auto write_request = GetWriteRequest(1, ElectionIdFromLower(1), DeviceId());
  ASSERT_OK_AND_ASSIGN(auto stub2, Stub());
  // Assert that we cannot write to slave device.
  ASSERT_FALSE(pdpi::SendPiWriteRequest(stub2.get(), write_request).ok());
}

TEST_P(MasterArbitrationTestFixture, SlaveCanRead) {
  // TODO Slave read failure.
  if (TestEnvironment().MaskKnownFailures()) GTEST_SKIP();

  TestEnvironment().SetTestCaseID("fb678921-d150-4535-b7b8-fc8cecb79a78");

  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));
  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  auto session = BecomeMaster(std::move(stub), 0);
  ASSERT_THAT(session.status(), NotMaster());

  ReadRequest read_everything = gutil::ParseProtoOrDie<ReadRequest>(R"pb(
    entities { table_entry { meter_config {} } }
  )pb");
  read_everything.set_device_id(DeviceId());
  ::grpc::ClientContext context;
  std::unique_ptr<::grpc::ClientReader<ReadResponse>> response_stream =
      (*session)->Stub().Read(&context, read_everything);
  ReadResponse response;
  EXPECT_TRUE(response_stream->Read(&response));
  // The switch should always return some const entries.
  ASSERT_FALSE(response.entities().empty());
}

TEST_P(MasterArbitrationTestFixture, GetNotifiedOfActualMaster) {
  TestEnvironment().SetTestCaseID("46b83014-759b-4393-bb58-220c0ca38711");
  ASSERT_OK_AND_ASSIGN(auto connection, BecomeMaster(1));

  // Assemble arbitration request.
  ::p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(DeviceId());
  arbitration->mutable_election_id()->set_high(
      absl::Uint128High64(ElectionIdFromLower(0)));
  arbitration->mutable_election_id()->set_low(
      absl::Uint128Low64(ElectionIdFromLower(0)));

  // Send arbitration request.
  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  grpc::ClientContext context;
  auto stream_channel = stub->StreamChannel(&context);
  stream_channel->Write(request);

  // Wait for arbitration response.
  ::p4::v1::StreamMessageResponse response;
  ASSERT_TRUE(stream_channel->Read(&response));
  EXPECT_EQ(response.update_case(), StreamMessageResponse::kArbitration);
  EXPECT_EQ(response.arbitration().device_id(), DeviceId());
  EXPECT_EQ(response.arbitration().election_id().high(),
            absl::Uint128High64(ElectionIdFromLower(1)));
  EXPECT_EQ(response.arbitration().election_id().low(),
            absl::Uint128Low64(ElectionIdFromLower(1)));
  EXPECT_EQ(response.arbitration().status().code(), ALREADY_EXISTS);
}

TEST_P(MasterArbitrationTestFixture, ZeroIdControllerCannotBecomeMaster) {
  // TODO Investigate the skipped test.
  if (TestEnvironment().MaskKnownFailures()) GTEST_SKIP();
  TestEnvironment().SetTestCaseID("3699fc43-5ff8-44ee-8965-68f42c71c1ed");

  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  // Connects controller C1 with id=0 and checks C1 is NOT master.
  auto session = BecomeMaster(std::move(stub), 0);
  ASSERT_THAT(session.status(), NotMaster());

  // Checks C1 with id=0 cannot write.
  auto write_request = GetWriteRequest(0, ElectionIdFromLower(0), DeviceId());
  ASSERT_THAT(pdpi::SendPiWriteRequest(&(*session)->Stub(), write_request),
              NotMaster());
}

TEST_P(MasterArbitrationTestFixture, OldMasterCannotWriteAfterNewMasterCameUp) {
  // TODO Investigate the skipped test.
  if (TestEnvironment().MaskKnownFailures()) GTEST_SKIP();

  TestEnvironment().SetTestCaseID("e4bc86a2-84f0-450a-888a-8a6f5f26fa8c");

  int id1 = 1, id2 = 2;
  // Connects controller C1 with id=1 to become master.
  ASSERT_OK_AND_ASSIGN(auto c1, BecomeMaster(id1));
  ASSERT_OK(pdpi::ClearTableEntries(c1.get(), IrP4Info()));

  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  EXPECT_THAT(
      pdpi::SendPiWriteRequest(
          stub.get(), GetWriteRequest(0, ElectionIdFromLower(id1), DeviceId())),
      gutil::IsOk());

  // Connects controller C2 with id=2 > 1 to become master.
  ASSERT_OK_AND_ASSIGN(auto c2, BecomeMaster(id2));
  ASSERT_OK(pdpi::ClearTableEntries(c2.get(), IrP4Info()));

  // Checks new master C2 can write.
  EXPECT_THAT(
      pdpi::SendPiWriteRequest(
          stub.get(), GetWriteRequest(1, ElectionIdFromLower(id2), DeviceId())),
      gutil::IsOk());

  // Checks C1 cannot write after new master C2 came up.
  EXPECT_THAT(
      pdpi::SendPiWriteRequest(
          stub.get(), GetWriteRequest(2, ElectionIdFromLower(id1), DeviceId())),
      NotMaster());
}

TEST_P(MasterArbitrationTestFixture, MasterDowngradesItself) {
  // TODO Investigate the skipped test.
  if (TestEnvironment().MaskKnownFailures()) GTEST_SKIP();

  TestEnvironment().SetTestCaseID("3cb62c0f-4a1a-430c-978c-a3a2a11078cd");

  int id1 = 1, id2 = 2;

  // Connects controller C2 with id=2 to become master.
  ASSERT_OK_AND_ASSIGN(auto controller, BecomeMaster(id2));
  ASSERT_OK(pdpi::ClearTableEntries(controller.get(), IrP4Info()));

  ASSERT_OK_AND_ASSIGN(auto stub, Stub());
  // Checks new master C2 can write.
  ASSERT_THAT(
      pdpi::SendPiWriteRequest(
          stub.get(), GetWriteRequest(0, ElectionIdFromLower(id2), DeviceId())),
      gutil::IsOk());

  // C2 sends master arbitration request with id=1 to downgrade itself.
  ::p4::v1::StreamMessageRequest request;
  auto arbitration = request.mutable_arbitration();
  arbitration->set_device_id(DeviceId());
  arbitration->mutable_election_id()->set_high(
      absl::Uint128High64(ElectionIdFromLower(id1)));
  arbitration->mutable_election_id()->set_low(
      absl::Uint128Low64(ElectionIdFromLower(id1)));

  grpc::ClientContext context;
  auto stream_channel = stub->StreamChannel(&context);
  stream_channel->Write(request);

  // Checks C2 cannot write after downgrading.
  EXPECT_THAT(
      pdpi::SendPiWriteRequest(
          stub.get(), GetWriteRequest(1, ElectionIdFromLower(id1), DeviceId())),
      NotMaster());
}

}  // namespace
}  // namespace gpins

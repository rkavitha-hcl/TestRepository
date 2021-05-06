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
#include <memory>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/numeric/int128.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/proto.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/netaddr/ipv4_address.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

DEFINE_bool(push_config, false, "Push P4 Info config file");
DEFINE_int32(batch_size, 1000, "Number of entries in each batch");
DEFINE_int32(number_batches, 10, "Number of batches");
DEFINE_int64(election_id, -1, "Election id to be used");

namespace p4rt_app {
namespace {

using ::testing::Test;

static constexpr absl::string_view router_interface = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554497
        match {
          field_id: 1
          exact { value: "1" }
        }
        action {
          action {
            action_id: 16777218
            params { param_id: 1 value: "1" }
            params { param_id: 2 value: "\000\002\003\004\005\005" }
          }
        }
      }
    }
  }
)pb";

static constexpr absl::string_view neighbor_entry = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554496
        match {
          field_id: 1
          exact { value: "1" }
        }
        match {
          field_id: 2
          exact { value: "10.0.0.1" }
        }
        action {
          action {
            action_id: 16777217
            params { param_id: 1 value: "\000\032\021\027_\200" }
          }
        }
      }
    }
  }
)pb";

static constexpr absl::string_view nexthop_entry = R"pb(
  updates {
    type: INSERT
    entity {
      table_entry {
        table_id: 33554498
        match {
          field_id: 1
          exact { value: "8" }
        }
        action {
          action {
            action_id: 16777219
            params { param_id: 1 value: "1" }
            params { param_id: 2 value: "10.0.0.1" }
          }
        }
      }
    }
  }
)pb";

static constexpr absl::string_view ip4table_entry = R"pb(
  type: $0
  entity {
    table_entry {
      table_id: 33554500
      match {
        field_id: 1
        exact { value: "12" }
      }
      match {
        field_id: 2
        lpm { value: "" prefix_len: 32 }
      }
      action {
        action {
          action_id: 16777221
          params { param_id: 1 value: "8" }
        }
      }
    }
  }
)pb";

class P4rtRouteTest : public Test {
 protected:
  absl::Status ProgramRequest(absl::string_view request_str,
                              p4::v1::Update::Type type) {
    p4::v1::WriteRequest request;
    RETURN_IF_ERROR(
        gutil::ReadProtoFromString(std::string{request_str}, &request));
    request.mutable_updates(0)->set_type(type);
    RETURN_IF_ERROR(
        pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request))
        << "Failed to program the request: " << request.ShortDebugString();
    return absl::OkStatus();
  }

  void SetUp() override {
    // Create connection to P4RT server.
    auto stub = pdpi::CreateP4RuntimeStub(
        /*address=*/"127.0.0.1:9559", grpc::InsecureChannelCredentials());

    absl::uint128 election_id = absl::MakeUint128(
        (FLAGS_election_id == -1 ? absl::ToUnixSeconds(absl::Now())
                                 : FLAGS_election_id),
        0);
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_,
        pdpi::P4RuntimeSession::Create(
            std::move(stub),
            /*device_id=*/183807201,
            pdpi::P4RuntimeSessionOptionalArgs{.election_id = election_id}));
    // Push P4 Info Config file if specified.
    if (FLAGS_push_config) {
      ASSERT_OK(pdpi::SetForwardingPipelineConfig(
          p4rt_session_.get(),
          sai::GetP4Info(sai::Instantiation::kMiddleblock)));
    }
    // Create the dependancy objects for ROUTE_ENTRY.
    // Create Router Intf object.
    ASSERT_OK(ProgramRequest(router_interface, p4::v1::Update::INSERT));
    // Create neighbor object.
    ASSERT_OK(ProgramRequest(neighbor_entry, p4::v1::Update::INSERT));
    // Create nexthop table entry.
    ASSERT_OK(ProgramRequest(nexthop_entry, p4::v1::Update::INSERT));
  }

  void TearDown() override {
    if (p4rt_session_ == nullptr) return;
    // Remove the depenancy objects that were created, in the reverse order.
    ASSERT_OK(ProgramRequest(nexthop_entry, p4::v1::Update::DELETE));
    ASSERT_OK(ProgramRequest(neighbor_entry, p4::v1::Update::DELETE));
    ASSERT_OK(ProgramRequest(router_interface, p4::v1::Update::DELETE));
  }

  absl::Status SendBatchRequest(absl::string_view iptable_entry,
                                absl::string_view update_type,
                                uint32_t number_batches, uint32_t batch_size) {
    uint32_t ip_prefix = 0x14000000;
    uint32_t subnet0, subnet1, subnet2, subnet3;
    for (uint32_t i = 0; i < number_batches; i++) {
      p4::v1::WriteRequest request;
      for (uint32_t j = 0; j < batch_size; j++) {
        subnet3 = ip_prefix & 0xff;
        subnet2 = (ip_prefix >> 8) & 0xff;
        subnet1 = (ip_prefix >> 16) & 0xff;
        subnet0 = (ip_prefix >> 24);
        const std::string ip_str =
            absl::Substitute("$0.$1.$2.$3", subnet0, subnet1, subnet2, subnet3);
        ASSIGN_OR_RETURN(const auto ip_address,
                         netaddr::Ipv4Address::OfString(ip_str));
        const auto ip_byte_str = ip_address.ToP4RuntimeByteString();
        auto* ptr = request.add_updates();
        if (!google::protobuf::TextFormat::ParseFromString(
                absl::Substitute(iptable_entry, update_type), ptr)) {
          return gutil::InvalidArgumentErrorBuilder()
                 << "Could not parse text as P4 Update for Ip address: "
                 << ip_byte_str;
        }
        ptr->mutable_entity()
            ->mutable_table_entry()
            ->mutable_match(1)
            ->mutable_lpm()
            ->set_value(ip_byte_str);
        ip_prefix++;
      }
      // Send a batch of requests to the server.
      RETURN_IF_ERROR(
          pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session_.get(), request));
    }
    return absl::OkStatus();
  }

  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(P4rtRouteTest, ProgramIp4RouteEntries) {
  auto start = absl::Now();
  auto status = SendBatchRequest(ip4table_entry, "INSERT", FLAGS_number_batches,
                                 FLAGS_batch_size);
  auto delta_time = absl::Now() - start;
  if (status.ok()) {
    // Send to stdout so that callers can parse the output.
    std::cout << "Successfully wrote IpTable entries to the switch, time: "
              << ToInt64Milliseconds(delta_time) << "(msecs)" << std::endl;
  }
  EXPECT_OK(status) << "Failed to add batch request";

  // Delete all batches, no matter the create passed or failed.
  status.Update(SendBatchRequest(ip4table_entry, "DELETE", FLAGS_number_batches,
                                 FLAGS_batch_size));
  EXPECT_OK(status) << "Failed to delete batch request";
}

}  // namespace
}  // namespace p4rt_app

// Temporary fix to have performance tests run nightly until we find a way to
// bring p4rt_test_main.cc to p4rt_app specific tests alone.
GTEST_API_ int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

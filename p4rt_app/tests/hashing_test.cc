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

#include "p4rt_app/sonic/hashing.h"

#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/connection_management.h"
#include "p4_pdpi/entity_management.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::_;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

static constexpr char kSampleP4Info[] = R"pb(
  actions {
    preamble {
      id: 17825802
      name: "ingress.hashing.select_emcp_hash_algorithm"
      alias: "select_emcp_hash_algorithm"
      annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
      annotations: "@sai_hash_seed(0)"
      annotations: "@sai_hash_offset(0)"
    }
  }
  actions {
    preamble {
      id: 16777227
      name: "ingress.hashing.compute_ecmp_hash_ipv4"
      alias: "compute_ecmp_hash_ipv4"
      annotations: "@sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV4)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV4)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV4)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)"
    }
  }
  actions {
    preamble {
      id: 16777228
      name: "ingress.hashing.compute_ecmp_hash_ipv6"
      alias: "compute_ecmp_hash_ipv6"
      annotations: "@sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IPV6)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV6)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV6)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)"
      annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)"
    }
  })pb";

class HashingTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("localhost:", p4rt_service_.GrpcPort());
    LOG(INFO) << "Opening P4RT connection to " << address << ".";
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(HashingTest, InsertAllHashTableAndSwitchTableOk) {
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(sai::Instantiation::kMiddleblock)));
  auto hash_field_keys = p4rt_service_.GetHashAppDbTable().GetAllKeys();
  for (const auto& key : hash_field_keys) {
    if (sonic::IsIpv4HashKey(key)) {
      EXPECT_THAT(
          p4rt_service_.GetHashAppDbTable().ReadTableEntry(key),
          IsOkAndHolds(Contains(Pair(
              "hash_field_list",
              "[\"src_ip\",\"dst_ip\",\"l4_src_port\",\"l4_dst_port\"]"))));
      // TODO: Enable after OrchAgent support.
    } else if (sonic::IsIpv6HashKey(key)) {
      EXPECT_THAT(
          p4rt_service_.GetHashAppDbTable().ReadTableEntry(key),
          IsOkAndHolds(Contains(Pair(
              "hash_field_list",
              "[\"src_ip\",\"dst_ip\",\"l4_src_port\",\"l4_dst_port\"]"))));
    } else {
      FAIL() << "Unexpected key " << key
             << " present for Ecmp hash fields in P4Info";
    }
  }
  EXPECT_THAT(p4rt_service_.GetSwitchAppDbTable().ReadTableEntry("switch"),
              IsOkAndHolds(UnorderedElementsAre(
                  Pair("ecmp_hash_algorithm", _), Pair("ecmp_hash_seed", _),
                  // TODO: Enable after OrchAgent support.
                  /*Pair("ecmp_hash_offset", _),*/ Pair("ecmp_hash_ipv6", _),
                  Pair("ecmp_hash_ipv4", _))));
}

TEST_F(HashingTest, VerifySwitchTableValuesOk) {
  p4::config::v1::P4Info p4_info;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kSampleP4Info, &p4_info));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      p4_info));
  EXPECT_THAT(
      p4rt_service_.GetSwitchAppDbTable().ReadTableEntry("switch"),
      IsOkAndHolds(UnorderedElementsAre(
          Pair("ecmp_hash_algorithm", "crc_32lo"), Pair("ecmp_hash_seed", "0"),
          // TODO: Enable after OrchAgent support.
          /*Pair("ecmp_hash_offset", _),*/ Pair("ecmp_hash_ipv6", _),
          Pair("ecmp_hash_ipv4", "compute_ecmp_hash_ipv4"))));
}

TEST_F(HashingTest, HashTableInsertionFails) {
  p4::config::v1::P4Info p4_info;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kSampleP4Info, &p4_info));
  p4rt_service_.GetHashAppDbTable().SetResponseForKey(
      "compute_ecmp_hash_ipv4", "SWSS_RC_INVALID_PARAM", "my error message");
  EXPECT_THAT(
      pdpi::SetForwardingPipelineConfig(
          p4rt_session_.get(),
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4_info),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("my error message")));
}

TEST_F(HashingTest, SwitchTableInsertionFails) {
  p4::config::v1::P4Info p4_info;
  EXPECT_TRUE(
      google::protobuf::TextFormat::ParseFromString(kSampleP4Info, &p4_info));
  p4rt_service_.GetSwitchAppDbTable().SetResponseForKey(
      "switch", "SWSS_RC_INVALID_PARAM", "my error message");
  EXPECT_THAT(
      pdpi::SetForwardingPipelineConfig(
          p4rt_session_.get(),
          p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
          p4_info),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("my error message")));
}

}  // namespace
}  // namespace p4rt_app

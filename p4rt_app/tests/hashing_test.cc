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
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "grpcpp/security/credentials.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsSupersetOf;
using ::testing::Key;
using ::testing::Pair;

// The ECMP hashing test verifies a P4 instance has a valid configuration for
// ECMP.
using EcmpHashingTest = testing::TestWithParam<sai::Instantiation>;

TEST_P(EcmpHashingTest, MustConfigureEcmpHashing) {
  // Start the P4RT service
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(P4RuntimeImplOptions{});

  // Create a P4RT session, and connect.
  std::string address = absl::StrCat("localhost:", p4rt_service.GrpcPort());
  auto stub =
      pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session,
                       pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));

  // Push the P4Info for the instance under test.
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(GetParam())));

  // Verify the AppDb HASH_TABLE has entries for both the IPv4 and IPv6
  // configurations.
  EXPECT_THAT(
      p4rt_service.GetHashAppDbTable().ReadTableEntry("compute_ecmp_hash_ipv4"),
      IsOkAndHolds(Contains(
          Pair("hash_field_list",
               R"(["src_ip","dst_ip","l4_src_port","l4_dst_port"])"))));
  EXPECT_THAT(
      p4rt_service.GetHashAppDbTable().ReadTableEntry("compute_ecmp_hash_ipv6"),
      IsOkAndHolds(Contains(Pair(
          "hash_field_list",
          R"(["src_ip","dst_ip","l4_src_port","l4_dst_port","ipv6_flow_label"])"))));

  // Verify the AppDb SWITCH_TABLE has an entry for all the ECMP configuration
  // fields.
  EXPECT_THAT(p4rt_service.GetSwitchAppDbTable().ReadTableEntry("switch"),
              IsOkAndHolds(IsSupersetOf({
                  Key("ecmp_hash_algorithm"),
                  Key("ecmp_hash_seed"),
                  Key("ecmp_hash_offset"),
                  Key("ecmp_hash_ipv6"),
                  Key("ecmp_hash_ipv4"),
              })));
}

INSTANTIATE_TEST_SUITE_P(
    EcmpHashingTestInstance, EcmpHashingTest,
    testing::Values(sai::Instantiation::kMiddleblock,
                    sai::Instantiation::kFabricBorderRouter),
    [](const testing::TestParamInfo<EcmpHashingTest::ParamType>& param) {
      return sai::InstantiationToString(param.param);
    });

// The LAG hashing test verifies a P4 instance has a valid configuration for
// LAGs.
using LagHashingTest = testing::TestWithParam<sai::Instantiation>;

// TODO: enable lag hash testing for FBR.
TEST_P(LagHashingTest, DISABLED_MustConfigureLagHashing) {
  // Start the P4RT service
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(P4RuntimeImplOptions{});

  // Create a P4RT session, and connect.
  std::string address = absl::StrCat("localhost:", p4rt_service.GrpcPort());
  auto stub =
      pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
  ASSERT_OK_AND_ASSIGN(std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session,
                       pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));

  // Push the P4Info for the instance under test.
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      sai::GetP4Info(GetParam())));

  // Verify the AppDb HASH_TABLE has entries for both the IPv4 and IPv6
  // configurations.
  EXPECT_THAT(
      p4rt_service.GetHashAppDbTable().ReadTableEntry("compute_lag_hash_ipv4"),
      IsOkAndHolds(Contains(
          Pair("hash_field_list",
               R"(["src_ip","dst_ip","l4_src_port","l4_dst_port"])"))));
  EXPECT_THAT(
      p4rt_service.GetHashAppDbTable().ReadTableEntry("compute_lag_hash_ipv6"),
      IsOkAndHolds(Contains(Pair(
          "hash_field_list",
          R"(["src_ip","dst_ip","l4_src_port","l4_dst_port","ipv6_flow_label"])"))));

  // Verify the AppDb SWITCH_TABLE has an entry for all the lag configuration
  // fields.
  EXPECT_THAT(p4rt_service.GetSwitchAppDbTable().ReadTableEntry("switch"),
              IsOkAndHolds(IsSupersetOf({
                  Key("lag_hash_algorithm"),
                  Key("lag_hash_seed"),
                  Key("lag_hash_offset"),
                  Key("lag_hash_ipv6"),
                  Key("lag_hash_ipv4"),
              })));
}

INSTANTIATE_TEST_SUITE_P(
    LagHashingTestInstance, LagHashingTest,
    testing::Values(sai::Instantiation::kFabricBorderRouter),
    [](const testing::TestParamInfo<LagHashingTest::ParamType>& param) {
      return sai::InstantiationToString(param.param);
    });

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

  // Sample hashing config for both ECMP and LAGs.
  static constexpr char kSampleP4Info[] = R"pb(
    actions {
      preamble {
        id: 17825802
        name: "ingress.hashing.select_ecmp_hash_algorithm"
        alias: "select_ecmp_hash_algorithm"
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
    }
  )pb";

  test_lib::P4RuntimeGrpcService p4rt_service_ =
      test_lib::P4RuntimeGrpcService(P4RuntimeImplOptions{});
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

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

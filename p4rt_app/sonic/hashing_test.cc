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
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::gutil::StatusIs;
using ::testing::Pointwise;
using ::testing::Test;
using ::testing::UnorderedPointwise;

MATCHER(FieldPairsAre, "") {
  return std::get<0>(arg).first == std::get<1>(arg).first &&
         std::get<0>(arg).second == std::get<1>(arg).second;
}

MATCHER(HashFieldsAreEqual, "") {
  const EcmpHashEntry& a = std::get<0>(arg);
  const EcmpHashEntry& b = std::get<1>(arg);
  return ExplainMatchResult(a.hash_key, b.hash_key, result_listener) &&
         ExplainMatchResult(Pointwise(FieldPairsAre(), a.hash_value),
                            b.hash_value, result_listener);
}

MATCHER_P(HashValuesAreEqual, check_field_value, "") {
  const swss::FieldValueTuple& a = std::get<0>(arg);
  const swss::FieldValueTuple& b = std::get<1>(arg);
  if (check_field_value) {
    return a.first == b.first && a.second == b.second;
  } else {
    return a.first == b.first;
  }
}

TEST(HashingTest, GenerateAppDbHashFieldEntriesOk) {
  pdpi::IrP4Info ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock);
  std::vector<EcmpHashEntry> expected_hash_fields = {
      {"compute_ecmp_hash_ipv6",
       {{"hash_field_list",
         "[\"src_ipv6\",\"dst_ipv6\",\"l4_src_port\",\"l4_dst_port\"]"}}},
      {"compute_ecmp_hash_ipv4",
       {{"hash_field_list",
         "[\"src_ip\",\"dst_ip\",\"l4_src_port\",\"l4_dst_port\"]"}}}};
  ASSERT_OK_AND_ASSIGN(auto actual_hash_fields,
                       GenerateAppDbHashFieldEntries(ir_p4_info));
  EXPECT_THAT(actual_hash_fields,
              UnorderedPointwise(HashFieldsAreEqual(), expected_hash_fields));
}

TEST(HashingTest, GenerateAppDbHashFieldEntriesNoSaiHashFields) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "NoAction"
             value {
               preamble {
                 id: 21257015
                 name: "NoAction"
                 alias: "NoAction"
                 annotations: "@noWarn(\"unused\")"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashFieldEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HashingTest, GenerateAppDbHashFieldEntriesExcessFields) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "compute_ecmp_hash_ipv4"
             value {
               preamble {
                 id: 16777227
                 name: "ingress.hashing.compute_ecmp_hash_ipv4"
                 alias: "compute_ecmp_hash_ipv4"
                 annotations: "@sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IP4)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_SRC_IPV4, SAI_NATIVE_HASH_FIELD_DST_IPV4)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(
      GenerateAppDbHashFieldEntries(ir_p4_info),
      StatusIs(absl::StatusCode::kInvalidArgument,
               testing::HasSubstr(
                   "Unexpected number of native hash field specified")));
}

TEST(HashingTest, GenerateAppDbHashFieldEntriesWrongIdentifier) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "compute_ecmp_hash_ipv4"
             value {
               preamble {
                 id: 16777227
                 name: "ingress.hashing.compute_ecmp_hash_ipv4"
                 alias: "compute_ecmp_hash_ipv4"
                 annotations: "@sai_ecmp_hash(SAI_SWITCH_ATTR_ECMP_HASH_IP4)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_WRONG_SRC_IP_IDENTIFIER)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_DST_IPV4)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_SRC_PORT)"
                 annotations: "@sai_native_hash_field(SAI_NATIVE_HASH_FIELD_L4_DST_PORT)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashFieldEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Unable to find hash field string")));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesOk) {
  const pdpi::IrP4Info ir_p4_info =
      sai::GetIrP4Info(sai::SwitchRole::kMiddleblock);
  std::vector<swss::FieldValueTuple> expected_hash_value = {
      {"ecmp_hash_algorithm", ""}, {"ecmp_hash_seed", ""},
      /*{"ecmp_hash_offset", ""}*/};
  ASSERT_OK_AND_ASSIGN(auto actual_hash_value,
                       GenerateAppDbHashValueEntries(ir_p4_info));
  EXPECT_THAT(actual_hash_value, UnorderedPointwise(HashValuesAreEqual(false),
                                                    expected_hash_value));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesWithFieldsOk) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
                 annotations: "@sai_hash_seed(1)"
                 annotations: "@sai_hash_offset(2)"
               }
             }
           })pb",
      &ir_p4_info));
  std::vector<swss::FieldValueTuple> expected_hash_value = {
      {"ecmp_hash_algorithm", "crc_32lo"}, {"ecmp_hash_seed", "1"},
      /*{"ecmp_hash_offset", "2"}*/};
  ASSERT_OK_AND_ASSIGN(auto actual_hash_value,
                       GenerateAppDbHashValueEntries(ir_p4_info));
  EXPECT_THAT(actual_hash_value, UnorderedPointwise(HashValuesAreEqual(true),
                                                    expected_hash_value));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesUnsupportedAlg) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_algorithm(UNSUPPORTED)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashValueEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kNotFound,
                       testing::HasSubstr("Unable to find hash algorithm")));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesDuplicateAlg) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
                 annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
                 annotations: "@sai_hash_offset(2)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashValueEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Duplicate hash algorithm type")));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesNoAlgorithm) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_seed(1)"
                 annotations: "@sai_hash_offset(2)"
               }
             }

           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashValueEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(HashingTest, GenerateAppDbHashValueEntriesDuplicateSeed) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
                 annotations: "@sai_hash_seed(0)"
                 annotations: "@sai_hash_seed(1)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashValueEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Duplicate hash algorithm seed")));
}

// TODO: Enable after OrchAgent support.
TEST(HashingTest, DISABLED_GenerateAppDbHashValueEntriesDuplicateOffset) {
  pdpi::IrP4Info ir_p4_info;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(actions_by_name {
             key: "select_emcp_hash_algorithm"
             value {
               preamble {
                 id: 17825802
                 name: "ingress.hashing.select_emcp_hash_algorithm"
                 alias: "select_emcp_hash_algorithm"
                 annotations: "@sai_hash_algorithm(SAI_HASH_ALGORITHM_CRC_32LO)"
                 annotations: "@sai_hash_offset(0)"
                 annotations: "@sai_hash_offset(1)"
               }
             }
           })pb",
      &ir_p4_info));
  EXPECT_THAT(GenerateAppDbHashValueEntries(ir_p4_info),
              StatusIs(absl::StatusCode::kInvalidArgument,
                       testing::HasSubstr("Duplicate hash algorithm offset")));
}

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

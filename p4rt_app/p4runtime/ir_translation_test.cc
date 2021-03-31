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
#include "p4rt_app/p4runtime/ir_translation.h"

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "boost/bimap.hpp"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/switch_role.h"

namespace p4rt_app {
namespace {

using ::google::protobuf::TextFormat;
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;

TEST(PortTranslationTest, TranslatePort) {
  boost::bimap<std::string, std::string> map;
  map.insert({"key0", "val0"});
  map.insert({"key1", "val1"});
  EXPECT_THAT(TranslatePort(TranslationDirection::kForController, map, "key0"),
              IsOkAndHolds("val0"));
  EXPECT_THAT(TranslatePort(TranslationDirection::kForOrchAgent, map, "val0"),
              IsOkAndHolds("key0"));
}

TEST(PortTranslationTest, TranslatePortFailsWithMissingKey) {
  boost::bimap<std::string, std::string> map;
  map.insert({"key0", "val0"});
  map.insert({"key1", "val1"});
  EXPECT_THAT(TranslatePort(TranslationDirection::kForController, map, "key2"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(TranslatePort(TranslationDirection::kForOrchAgent, map, "val2"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PortTranslationTest, ActionParameters) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "router_interface_table"
        action {
          name: "set_port_and_src_mac"
          params {
            name: "port"
            value { str: "1" }
          }
        })pb",
      &table_entry));
  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));
  ASSERT_EQ(table_entry.action().params_size(), 1);
  EXPECT_EQ(table_entry.action().params(0).value().str(), "Ethernet0");
}

TEST(PortTranslationTest, ActionSetParameters) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(
                                            table_name: "wcmp_group_table"
                                            action_set {
                                              actions {
                                                action {
                                                  name: "set_nexthop_id"
                                                  params {
                                                    name: "nexthop_id"
                                                    value { str: "1" }
                                                  }
                                                }
                                                weight: 1
                                                watch_port: "2"
                                              }
                                            })pb",
                                          &table_entry));
  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));

  // Expect the watch_port to change.
  ASSERT_EQ(table_entry.action_set().actions_size(), 1);
  EXPECT_EQ(table_entry.action_set().actions(0).watch_port(), "Ethernet4");
}

TEST(PortTranslationTest, ExactMatchField) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "l3_admit_table"
        matches {
          name: "in_port"
          exact { str: "2" }
        })pb",
      &table_entry));

  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));
  ASSERT_EQ(table_entry.matches_size(), 1);
  EXPECT_EQ(table_entry.matches(0).exact().str(), "Ethernet4");
}

TEST(PortTranslationTest, OptionalMatchField) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(
                                            table_name: "acl_lookup_table"
                                            matches {
                                              name: "in_port"
                                              optional { value { str: "2" } }
                                            })pb",
                                          &table_entry));
  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));
  ASSERT_EQ(table_entry.matches_size(), 1);
  EXPECT_EQ(table_entry.matches(0).optional().value().str(), "Ethernet4");
}

TEST(VrfTranslationTest, ActionParameters) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "acl_lookup_table"
        action {
          name: "set_vrf"
          params {
            name: "vrf_id"
            value { str: "vrf-1" }
          }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));
  ASSERT_EQ(table_entry.action().params_size(), 1);
  EXPECT_EQ(table_entry.action().params(0).value().str(), "p4rt-vrf-1");
}

TEST(VrfTranslationTest, ExactMatchField) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "ipv6_table"
        matches {
          name: "vrf_id"
          exact { str: "80" }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  ASSERT_OK(TranslateTableEntry(
      TranslateTableEntryOptions{
          .direction = TranslationDirection::kForOrchAgent,
          .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
          .port_map = translation_map},
      table_entry));
  ASSERT_EQ(table_entry.matches_size(), 1);
  EXPECT_EQ(table_entry.matches(0).exact().str(), "p4rt-80");
}

TEST(VrfTranslationTest, InvalidMatchTypeFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "ipv6_table"
        matches {
          name: "vrf_id"
          optional { value { str: "80" } }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(VrfTranslationTest, InvalidFieldFormatFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "ipv6_table"
        matches {
          name: "vrf_id"
          exact { hex_str: "80" }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IrTranslationTest, InvalidTableNameFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(R"pb(table_name: "sample_name"
                                               action {
                                                 name: "sample_action"
                                                 params {
                                                   name: "sample_param"
                                                   value { str: "1" }
                                                 }
                                               })pb",
                                          &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("sample_name")));
}

TEST(IrTranslationTest, InvalidMatchFieldNameFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "l3_admit_table"
        matches {
          name: "in_port"
          ternary {
            value { str: "2" }
            mask { str: "2" }
          }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(IrTranslationTest, InvalidMatchFieldTypeFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "l3_admit_table"
        matches {
          name: "sample_field"
          exact { str: "2" }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("sample_field")));
}

TEST(IrTranslationTest, InvalidActionNameFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "router_interface_table"
        action {
          name: "some_action"
          params {
            name: "port"
            value { str: "1" }
          }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("some_action")));
}

TEST(IrTranslationTest, InvalidActionParameterNameFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "router_interface_table"
        action {
          name: "set_port_and_src_mac"
          params {
            name: "some_param"
            value { str: "1" }
          }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForOrchAgent,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInternal, HasSubstr("some_param")));
}

TEST(IrTranslationTest, ActionParametersWithUnsupportedFormatFails) {
  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      R"pb(
        table_name: "router_interface_table"
        action {
          name: "set_port_and_src_mac"
          params {
            name: "port"
            value { hex_str: "1" }
          }
        })pb",
      &table_entry));

  boost::bimap<std::string, std::string> translation_map;
  EXPECT_THAT(
      TranslateTableEntry(
          TranslateTableEntryOptions{
              .direction = TranslationDirection::kForController,
              .ir_p4_info = sai::GetIrP4Info(sai::SwitchRole::kMiddleblock),
              .port_map = translation_map},
          table_entry),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

}  // namespace
}  // namespace p4rt_app

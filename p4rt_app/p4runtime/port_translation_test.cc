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
#include "p4rt_app/p4runtime/port_translation.h"

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "boost/bimap.hpp"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"

namespace p4rt_app {
namespace {

using ::google::protobuf::TextFormat;
using ::gutil::IsOkAndHolds;
using ::gutil::StatusIs;
using ::testing::HasSubstr;

const std::vector<std::string>& PortMatchFieldNames() {
  static const auto* const kNames = new std::vector<std::string>(
      {"port", "watch_port", "in_port", "out_port", "dst_port"});
  return *kNames;
}

// Test parameter names can only be alphanumeric. This function reformats
// string parameters into CamelCase by treating any non-alnum character as a
// word break.
std::string FormatParamName(absl::string_view param) {
  std::string param_name;
  param_name.reserve(param.size());
  bool capitalize = true;
  for (char c : param) {
    if (!absl::ascii_isalnum(c)) {
      capitalize = true;
      continue;
    }
    if (capitalize && absl::ascii_isalpha(c)) {
      c = absl::ascii_toupper(c);
      capitalize = false;
    }
    param_name.push_back(c);
  }
  return param_name;
}

TEST(PortTranslationTest, TranslatePort) {
  boost::bimap<std::string, std::string> map;
  map.insert({"key0", "val0"});
  map.insert({"key1", "val1"});
  EXPECT_THAT(
      TranslatePort(PortTranslationDirection::kForController, map, "key0"),
      IsOkAndHolds("val0"));
  EXPECT_THAT(
      TranslatePort(PortTranslationDirection::kForOrchAgent, map, "val0"),
      IsOkAndHolds("key0"));
}

TEST(PortTranslationTest, TranslatePortFailsWithMissingKey) {
  boost::bimap<std::string, std::string> map;
  map.insert({"key0", "val0"});
  map.insert({"key1", "val1"});
  EXPECT_THAT(
      TranslatePort(PortTranslationDirection::kForController, map, "key2"),
      StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(
      TranslatePort(PortTranslationDirection::kForOrchAgent, map, "val2"),
      StatusIs(absl::StatusCode::kInvalidArgument));
}

using SonicPortNameTranslationTest = ::testing::TestWithParam<std::string>;

TEST_P(SonicPortNameTranslationTest, ActionParameters) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(absl::Substitute(R"pb(
                                                     table_name: "sample_table"
                                                     action {
                                                       name: "sample_action"
                                                       params {
                                                         name: "$0"
                                                         value { str: "1" }
                                                       }
                                                     })pb",
                                                   GetParam()),
                                  &table_entry));
  ASSERT_OK(TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                                    translation_map, table_entry));
  ASSERT_EQ(table_entry.action().params_size(), 1);
  EXPECT_EQ(table_entry.action().params(0).value().str(), "Ethernet0");
}

TEST_P(SonicPortNameTranslationTest,
       ActionParametersWithUnsupportedFormatFails) {
  boost::bimap<std::string, std::string> translation_map;

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::Substitute(R"pb(
                         table_name: "sample_table"
                         action {
                           name: "sample_action"
                           params {
                             name: "$0"
                             value { hex_str: "0x1" }
                           }
                         })pb",
                       GetParam()),
      &table_entry));
  EXPECT_THAT(TranslatePortIdAndNames(PortTranslationDirection::kForController,
                                      translation_map, table_entry),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_P(SonicPortNameTranslationTest, ActionSetParameters) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::Substitute(R"pb(
                         table_name: "sample_table"
                         action_set {
                           actions {
                             action {
                               name: "sample_action0"
                               params {
                                 name: "$0"
                                 value { str: "1" }
                               }
                             }
                             weight: 1
                             watch_port: "2"
                           }
                         })pb",
                       GetParam()),
      &table_entry));
  ASSERT_OK(TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                                    translation_map, table_entry));

  // Expect the watch_port to change.
  ASSERT_EQ(table_entry.action_set().actions_size(), 1);
  EXPECT_EQ(table_entry.action_set().actions(0).watch_port(), "Ethernet4");

  // Expect the action paramter to also change.
  ASSERT_EQ(table_entry.action_set().actions(0).action().params_size(), 1);
  EXPECT_EQ(
      table_entry.action_set().actions(0).action().params(0).value().str(),
      "Ethernet0");
}

TEST_P(SonicPortNameTranslationTest, ExactMatchField) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(absl::Substitute(R"pb(
                                                     table_name: "sample_table"
                                                     matches {
                                                       name: "$0"
                                                       exact { str: "2" }
                                                     })pb",
                                                   GetParam()),
                                  &table_entry));
  ASSERT_OK(TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                                    translation_map, table_entry));
  ASSERT_EQ(table_entry.matches_size(), 1);
  EXPECT_EQ(table_entry.matches(0).exact().str(), "Ethernet4");
}

TEST_P(SonicPortNameTranslationTest, OptionalMatchField) {
  boost::bimap<std::string, std::string> translation_map;
  translation_map.insert({"Ethernet0", "1"});
  translation_map.insert({"Ethernet4", "2"});

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(TextFormat::ParseFromString(
      absl::Substitute(R"pb(
                         table_name: "sample_table"
                         matches {
                           name: "$0"
                           optional { value { str: "2" } }
                         })pb",
                       GetParam()),
      &table_entry));
  ASSERT_OK(TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                                    translation_map, table_entry));
  ASSERT_EQ(table_entry.matches_size(), 1);
  EXPECT_EQ(table_entry.matches(0).optional().value().str(), "Ethernet4");
}

TEST_P(SonicPortNameTranslationTest, InvalidMatchFieldTypeFails) {
  boost::bimap<std::string, std::string> translation_map;

  pdpi::IrTableEntry table_entry;
  ASSERT_TRUE(
      TextFormat::ParseFromString(absl::Substitute(R"pb(
                                                     table_name: "sample_table"
                                                     matches {
                                                       name: "$0"
                                                       ternary {
                                                         value { str: "2" }
                                                         mask { str: "2" }
                                                       }
                                                     })pb",
                                                   GetParam()),
                                  &table_entry));
  EXPECT_THAT(TranslatePortIdAndNames(PortTranslationDirection::kForOrchAgent,
                                      translation_map, table_entry),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

INSTANTIATE_TEST_SUITE_P(SonicPortNameTranslationInstance,
                         SonicPortNameTranslationTest,
                         ::testing::ValuesIn(PortMatchFieldNames()),
                         [](const ::testing::TestParamInfo<std::string>& info) {
                           return FormatParamName(info.param);
                         });

}  // namespace
}  // namespace p4rt_app

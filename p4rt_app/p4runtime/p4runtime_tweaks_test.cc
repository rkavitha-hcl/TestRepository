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

#include "p4rt_app/p4runtime/p4runtime_tweaks.h"

#include <string>

#include "absl/strings/ascii.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/utils/ir_builder.h"

namespace p4rt_app {
namespace {

using ::testing::Not;

TEST(P4RuntimeTweaksP4InfoTest, ForOrchAgentSetsCompositeUdfFormatToString) {
  const std::string match_field_proto_string =
      R"pb(id: 123
           name: "match_field_name"
           annotations: "@composite_field(@sai_udf(base=SAI_UDF_BASE_L3, offset=2, length=2), @sai_udf(base=SAI_UDF_BASE_L3, offset=4, length=2))")pb";

  pdpi::IrTableDefinition ir_table =
      IrTableDefinitionBuilder()
          .preamble(R"pb(alias: "Table" id: 1)pb")
          .match_field(match_field_proto_string, pdpi::IPV4)
          .entry_action(IrActionDefinitionBuilder().preamble(
              R"pb(alias: "action_name")pb"))
          .size(512)();

  pdpi::IrP4Info ir_p4_info;
  (*ir_p4_info.mutable_tables_by_id())[1] = ir_table;
  (*ir_p4_info.mutable_tables_by_name())["Table"] = ir_table;

  pdpi::IrTableDefinition ir_table_with_string_format =
      IrTableDefinitionBuilder()
          .preamble(R"pb(alias: "Table" id: 1)pb")
          .match_field(match_field_proto_string, pdpi::HEX_STRING)
          .entry_action(IrActionDefinitionBuilder().preamble(
              R"pb(alias: "action_name")pb"))
          .size(512)();

  P4RuntimeTweaks::ForOrchAgent(ir_p4_info);
  EXPECT_THAT(ir_p4_info.tables_by_id().at(1),
              gutil::EqualsProto(ir_table_with_string_format));
  EXPECT_THAT(ir_p4_info.tables_by_name().at("Table"),
              gutil::EqualsProto(ir_table_with_string_format));
}

// We're only testing non-udf & all-udf. Partial-udf isn't supported, so it does
// not need to be tested.
TEST(P4RuntimeTweaksP4InfoTest, ForOrchAgentIgnoresCompositeNonUdfFields) {
  const std::string match_field_proto_string =
      R"pb(id: 123
           name: "match_field_name"
           annotations: "@composite_field(@sai_field(SAI_FIELD1), @sai_field(SAI_FIELD2))")pb";

  pdpi::IrTableDefinition ir_table =
      IrTableDefinitionBuilder()
          .preamble(R"pb(alias: "Table" id: 1)pb")
          .match_field(match_field_proto_string, pdpi::IPV4)
          .entry_action(IrActionDefinitionBuilder().preamble(
              R"pb(alias: "action_name")pb"))
          .size(512)();

  pdpi::IrP4Info ir_p4_info;
  (*ir_p4_info.mutable_tables_by_id())[1] = ir_table;
  (*ir_p4_info.mutable_tables_by_name())["Table"] = ir_table;

  pdpi::IrP4Info unchanged_ir_p4_info = ir_p4_info;
  P4RuntimeTweaks::ForOrchAgent(ir_p4_info);
  EXPECT_THAT(ir_p4_info, gutil::EqualsProto(unchanged_ir_p4_info));
}

TEST(P4RuntimeTweaksTableEntryTest,
     ForOrchAgentReplacesNeighborIdMatchWithIPv6LinkLocalAddress) {
  pdpi::IrTableEntry entry;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name: "neighbor_table"
           matches {
             name: "neighbor_id"
             exact { str: "Neighbor0" }
           }
           action {
             params {
               name: "dst_mac"
               value { mac: "aa:bb:cc:dd:ee:ff" }
             }
           })pb",
      &entry);

  pdpi::IrTableEntry orchagent_entry = entry;
  orchagent_entry.mutable_matches(0)->mutable_exact()->set_str(
      "fe80::a8bb:ccff:fedd:eeff");

  P4RuntimeTweaks p4runtime_tweaks;
  EXPECT_THAT(p4runtime_tweaks.ForOrchAgent(entry),
              gutil::EqualsProto(orchagent_entry));
}

TEST(P4RuntimeTweaksTableEntryTest,
     ForControllerReplacesIPv6LinkLocalAddressMatchWithNeighborId) {
  pdpi::IrTableEntry controller_entry;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name: "neighbor_table"
           matches {
             name: "neighbor_id"
             exact { str: "Neighbor0" }
           }
           action {
             params {
               name: "dst_mac"
               value { mac: "aa:bb:cc:dd:ee:ff" }
             }
           })pb",
      &controller_entry);

  P4RuntimeTweaks p4runtime_tweaks;
  pdpi::IrTableEntry orchagent_entry =
      p4runtime_tweaks.ForOrchAgent(controller_entry);
  ASSERT_THAT(orchagent_entry,
              Not(gutil::EqualsProto(controller_entry)));  // Sanity check.

  EXPECT_THAT(p4runtime_tweaks.ForController(orchagent_entry),
              gutil::IsOkAndHolds(gutil::EqualsProto(controller_entry)));
}

TEST(P4RuntimeTweaksTableEntryTest,
     ForOrchAgentReplacesNeighborIdActionWithIPv6LinkLocalAddress) {
  P4RuntimeTweaks p4runtime_tweaks;

  // Prime the neighbor id.
  {
    pdpi::IrTableEntry prime_entry;
    google::protobuf::TextFormat::ParseFromString(
        R"pb(table_name: "neighbor_table"
             matches {
               name: "neighbor_id"
               exact { str: "Neighbor0" }
             }
             action {
               params {
                 name: "dst_mac"
                 value { mac: "aa:bb:cc:dd:ee:ff" }
               }
             })pb",
        &prime_entry);
    p4runtime_tweaks.ForOrchAgent(prime_entry);
  }

  pdpi::IrTableEntry entry;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name: "table"
           matches {
             name: "match_field"
             exact { str: "Match" }
           }
           action {
             params {
               name: "neighbor_id"
               value { str: "Neighbor0" }
             }
           })pb",
      &entry);

  pdpi::IrTableEntry orchagent_entry = entry;
  orchagent_entry.mutable_action()->mutable_params(0)->mutable_value()->set_str(
      "fe80::a8bb:ccff:fedd:eeff");

  EXPECT_THAT(p4runtime_tweaks.ForOrchAgent(entry),
              gutil::EqualsProto(orchagent_entry));
}

TEST(P4RuntimeTweaksTableEntryTest,
     ForControllerReplacesIPv6LinkLocalAddressActionWithNeighborId) {
  P4RuntimeTweaks p4runtime_tweaks;

  // Prime the neighbor id.
  {
    pdpi::IrTableEntry prime_entry;
    google::protobuf::TextFormat::ParseFromString(
        R"pb(table_name: "neighbor_table"
             matches {
               name: "neighbor_id"
               exact { str: "Neighbor0" }
             }
             action {
               params {
                 name: "dst_mac"
                 value { mac: "aa:bb:cc:dd:ee:ff" }
               }
             })pb",
        &prime_entry);
    p4runtime_tweaks.ForOrchAgent(prime_entry);
  }

  pdpi::IrTableEntry controller_entry;
  google::protobuf::TextFormat::ParseFromString(
      R"pb(table_name: "table"
           matches {
             name: "match_field"
             exact { str: "Match" }
           }
           action {
             params {
               name: "neighbor_id"
               value { str: "Neighbor0" }
             }
           })pb",
      &controller_entry);

  pdpi::IrTableEntry orchagent_entry =
      p4runtime_tweaks.ForOrchAgent(controller_entry);
  ASSERT_THAT(orchagent_entry,
              Not(gutil::EqualsProto(controller_entry)));  // Sanity check.
  EXPECT_THAT(p4runtime_tweaks.ForController(orchagent_entry),
              gutil::IsOkAndHolds(gutil::EqualsProto(controller_entry)));
}

}  // namespace
}  // namespace p4rt_app

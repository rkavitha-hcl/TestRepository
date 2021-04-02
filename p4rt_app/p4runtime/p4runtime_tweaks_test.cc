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

}  // namespace
}  // namespace p4rt_app

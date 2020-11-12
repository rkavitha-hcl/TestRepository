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

#include "p4_pdpi/sequencing.h"

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "boost/graph/adjacency_list.hpp"
#include "gutil/collections.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace pdpi {

namespace {
using ::p4::v1::Action_Param;
using ::p4::v1::Update;
using ::p4::v1::WriteRequest;

// We require boost::in_degree, which requires bidirectionalS.
using Graph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS>;
// Boost uses integers to identify vertices.
using Vertex = int;

// Describing a foreign key (table + match field) as well as the value of that
// match field (in this order).
using ForeignKeyValue = std::tuple<std::string, std::string, std::string>;
// The mapping of a foreign key value to vertices that are being referred to by
// that foreign key value.
using ForeignKeyValueToVertices =
    absl::flat_hash_map<ForeignKeyValue, absl::flat_hash_set<Vertex>>;

absl::StatusOr<absl::optional<std::string>> GetMatchFieldValue(
    const IrTableDefinition& ir_table_definition, const Update& update,
    const std::string& match_field) {
  ASSIGN_OR_RETURN(
      auto match_field_definition,
      gutil::FindOrStatus(ir_table_definition.match_fields_by_name(),
                          match_field),
      _ << "Failed to build dependency graph: Match field with name "
        << match_field << " does not exist.");
  int32_t match_field_id = match_field_definition.match_field().id();
  for (const auto& match : update.entity().table_entry().match()) {
    if (match.field_id() == match_field_id) {
      if (match.has_exact()) {
        return match.exact().value();
      } else if (match.has_optional()) {
        return match.optional().value();
      }
    }
  }
  return absl::nullopt;
}

// Records and updates the dependency graph for for the given action invocation.
absl::Status RecordDependenciesForActionInvocation(
    const std::vector<Update>& all_vertices,
    const IrActionDefinition& ir_action,
    absl::Span<const Action_Param* const> params, Vertex current_vertex,
    ForeignKeyValueToVertices& indices, Graph& graph) {
  for (const Action_Param* const param : params) {
    ASSIGN_OR_RETURN(
        const auto& param_definition,
        gutil::FindOrStatus(ir_action.params_by_id(), param->param_id()),
        _ << "Failed to build dependency graph: Aciton param with ID "
          << param->param_id() << " does not exist.");
    for (const auto& ir_foreign_key : param_definition.foreign_keys()) {
      ForeignKeyValue foreign_key_value = {
          ir_foreign_key.table(), ir_foreign_key.match_field(), param->value()};
      for (Vertex referred_update_index : indices[foreign_key_value]) {
        const Update& referred_update = all_vertices[referred_update_index];
        if ((all_vertices[current_vertex].type() == p4::v1::Update::INSERT ||
             all_vertices[current_vertex].type() == p4::v1::Update::MODIFY) &&
            referred_update.type() == p4::v1::Update::INSERT) {
          boost::add_edge(referred_update_index, current_vertex, graph);
        } else if (all_vertices[current_vertex].type() ==
                       p4::v1::Update::DELETE &&
                   referred_update.type() == p4::v1::Update::DELETE) {
          boost::add_edge(current_vertex, referred_update_index, graph);
        }
      }
    }
  }
  return absl::OkStatus();
}

// Builds the dependency graph between updates. An edge from u to v indicates
// that u must be sent in a batch before sending v.
absl::StatusOr<Graph> BuildDependencyGraph(const IrP4Info& info,
                                           const std::vector<Update>& updates) {
  // Graph containing one node per update.
  Graph graph(updates.size());

  // Build indices to map foreign keys to the set of updates of that key.
  ForeignKeyValueToVertices indices;
  for (int update_index = 0; update_index < updates.size(); update_index++) {
    const Update& update = updates[update_index];
    ASSIGN_OR_RETURN(
        const IrTableDefinition& ir_table_definition,
        gutil::FindOrStatus(info.tables_by_id(),
                            update.entity().table_entry().table_id()),
        _ << "Failed to build dependency graph: Table with ID "
          << update.entity().table_entry().table_id() << " does not exist.");
    const std::string& update_table_name =
        ir_table_definition.preamble().alias();
    for (const auto& ir_foreign_key : info.foreign_keys()) {
      if (update_table_name != ir_foreign_key.table()) continue;
      ASSIGN_OR_RETURN(absl::optional<std::string> value,
                       GetMatchFieldValue(ir_table_definition, update,
                                          ir_foreign_key.match_field()));
      if (value.has_value()) {
        ForeignKeyValue foreign_key_value = {ir_foreign_key.table(),
                                             ir_foreign_key.match_field(),
                                             value.value()};
        indices[foreign_key_value].insert(update_index);
      }
    }
  }

  // Build dependency graph.
  for (int update_index = 0; update_index < updates.size(); update_index++) {
    const Update& update = updates[update_index];
    const p4::v1::TableAction& action = update.entity().table_entry().action();

    switch (action.type_case()) {
      case p4::v1::TableAction::kAction: {
        ASSIGN_OR_RETURN(
            const IrActionDefinition ir_action,
            gutil::FindOrStatus(info.actions_by_id(),
                                action.action().action_id()),
            _ << "Failed to build dependency graph: Action with ID "
              << action.action().action_id() << " does not exist.");
        RETURN_IF_ERROR(RecordDependenciesForActionInvocation(
            updates, ir_action, action.action().params(), update_index, indices,
            graph));
        break;
      }
      case p4::v1::TableAction::kActionProfileActionSet: {
        const p4::v1::ActionProfileActionSet& action_profile_set =
            action.action_profile_action_set();

        for (const auto& action_profile :
             action_profile_set.action_profile_actions()) {
          ASSIGN_OR_RETURN(
              const IrActionDefinition ir_action,
              gutil::FindOrStatus(info.actions_by_id(),
                                  action_profile.action().action_id()),
              _ << "Failed to build dependency graph: Action with ID "
                << action_profile.action().action_id() << " does not exist.");
          RETURN_IF_ERROR(RecordDependenciesForActionInvocation(
              updates, ir_action, action_profile.action().params(),
              update_index, indices, graph));
        }
        break;
      }
      default: {
        return gutil::UnimplementedErrorBuilder()
               << "Only kAction and kActionProfileActionSet are supported: "
               << action.DebugString();
      }
    }
  }
  return graph;
}

}  // namespace

absl::StatusOr<std::vector<p4::v1::WriteRequest>>
SequencePiUpdatesIntoWriteRequests(const IrP4Info& info,
                                   const std::vector<Update>& updates) {
  std::vector<WriteRequest> requests;
  ASSIGN_OR_RETURN(const auto batches, SequencePiUpdatesInPlace(info, updates));
  for (const std::vector<int>& batch : batches) {
    WriteRequest request;
    for (int i : batch) *request.add_updates() = updates[i];
    requests.push_back(std::move(request));
  }
  return requests;
}

absl::StatusOr<std::vector<std::vector<int>>> SequencePiUpdatesInPlace(
    const IrP4Info& info, const std::vector<Update>& updates) {
  ASSIGN_OR_RETURN(Graph graph, BuildDependencyGraph(info, updates));

  std::vector<Vertex> roots;
  for (Vertex vertex : graph.vertex_set()) {
    if (boost::in_degree(vertex, graph) == 0) {
      roots.push_back(vertex);
    }
  }

  std::vector<std::vector<int>> batches;
  while (!roots.empty()) {
    // The roots have no incoming dependency edges, hence can be batched.
    batches.push_back(roots);

    // Remove edges for old roots and add new roots.
    std::vector<Vertex> new_roots;
    for (Vertex root : roots) {
      for (const auto& edge : graph.out_edge_list(root)) {
        Vertex target = edge.get_target();
        // Is this the final `edge` into `target`?
        if (boost::in_degree(target, graph) == 1) new_roots.push_back(target);
      }
      boost::clear_vertex(root, graph);
    }
    roots.swap(new_roots);
  }
  return batches;
}

// The implementation can be reduced to sorting INSERTS using
// SequencePiUpdatesIntoWriteRequests.
absl::Status SortTableEntries(const IrP4Info& info,
                              std::vector<p4::v1::TableEntry>& entries) {
  std::vector<Update> updates;
  for (const auto& entry : entries) {
    Update update;
    update.set_type(Update::INSERT);
    *update.mutable_entity()->mutable_table_entry() = entry;
    updates.push_back(update);
  }

  ASSIGN_OR_RETURN(std::vector<p4::v1::WriteRequest> requests,
                   SequencePiUpdatesIntoWriteRequests(info, updates));
  entries.clear();
  for (const auto& write_request : requests) {
    for (const auto& update : write_request.updates()) {
      entries.push_back(update.entity().table_entry());
    }
  }
  return absl::OkStatus();
}

}  // namespace pdpi

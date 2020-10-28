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
#include "absl/types/optional.h"
#include "boost/graph/adjacency_list.hpp"
#include "gutil/collections.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"

namespace pdpi {

namespace {
using ::p4::v1::Update;
using ::p4::v1::WriteRequest;

// We require boost::in_degree, which requires bidirectionalS.
using Graph =
    boost::adjacency_list<boost::vecS, boost::vecS, boost::bidirectionalS>;
// Boost uses integers to identify vertices.
using Vertex = int64_t;

// Describing a foreign key (table + match field) as well as the value of that
// match field (in this order).
using ForeignKeyValue = std::tuple<std::string, std::string, std::string>;

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

// Builds the dependency graph between updates. An edge from u to v indicates
// that u must be sent in a batch before sending v.
absl::StatusOr<Graph> BuildDependencyGraph(const IrP4Info& info,
                                           const std::vector<Update>& updates) {
  // Graph containing one node per update.
  Graph graph(updates.size());

  // Build indices to map foreign keys to the set of updates of that key.
  absl::flat_hash_map<ForeignKeyValue, absl::flat_hash_set<Vertex>> indices;
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
    ASSIGN_OR_RETURN(
        const IrActionDefinition ir_action,
        gutil::FindOrStatus(info.actions_by_id(), action.action().action_id()),
        _ << "Failed to build dependency graph: Action with ID "
          << action.action().action_id() << " does not exist.");
    for (const auto& param : action.action().params()) {
      ASSIGN_OR_RETURN(
          const auto& param_definition,
          gutil::FindOrStatus(ir_action.params_by_id(), param.param_id()),
          _ << "Failed to build dependency graph: Aciton param with ID "
            << param.param_id() << " does not exist.");
      for (const auto& ir_foreign_key : param_definition.foreign_keys()) {
        ForeignKeyValue foreign_key_value = {ir_foreign_key.table(),
                                             ir_foreign_key.match_field(),
                                             param.value()};
        for (Vertex referred_update_index : indices[foreign_key_value]) {
          const Update& referred_update = updates[referred_update_index];
          if ((update.type() == p4::v1::Update::INSERT ||
               update.type() == p4::v1::Update::MODIFY) &&
              referred_update.type() == p4::v1::Update::INSERT) {
            boost::add_edge(referred_update_index, update_index, graph);
          } else if (update.type() == p4::v1::Update::DELETE &&
                     referred_update.type() == p4::v1::Update::DELETE) {
            boost::add_edge(update_index, referred_update_index, graph);
          }
        }
      }
    }
  }
  return graph;
}

}  // namespace

absl::StatusOr<std::vector<p4::v1::WriteRequest>> SequenceP4Updates(
    const IrP4Info& info, const std::vector<Update>& updates) {
  ASSIGN_OR_RETURN(Graph graph, BuildDependencyGraph(info, updates));

  std::vector<Vertex> roots;
  for (Vertex vertex : graph.vertex_set()) {
    if (boost::in_degree(vertex, graph) == 0) {
      roots.push_back(vertex);
    }
  }

  std::vector<WriteRequest> requests;
  while (!roots.empty()) {
    // New write request for the current roots.
    WriteRequest request;
    for (Vertex root : roots) {
      *request.add_updates() = updates[root];
    }
    requests.push_back(request);

    // Remove edges for old roots and add new roots.
    // Using btree_set to get deterministic order when iterating.
    absl::btree_set<Vertex> new_roots;
    for (Vertex root : roots) {
      for (const auto& edge : graph.out_edge_list(root)) {
        if (boost::in_degree(edge.get_target(), graph) == 1) {
          new_roots.insert(edge.get_target());
        }
      }
      boost::clear_vertex(root, graph);
    }
    roots.clear();
    for (Vertex new_root : new_roots) {
      roots.push_back(new_root);
    }
  }
  return requests;
}

}  // namespace pdpi

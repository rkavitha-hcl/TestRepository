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

#include "absl/strings/string_view.h"
#include "absl/time/clock.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/pd.h"
#include "p4_symbolic/parser.h"
#include "p4_symbolic/sai/fields.h"
#include "p4_symbolic/sai/parser.h"
#include "p4_symbolic/sai/sai.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "p4_symbolic/z3_util.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_nonstandard_platforms.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "thinkit/bazel_test_environment.h"

namespace p4_symbolic {
namespace {

using ::gutil::ParseProtoOrDie;
using ::testing::Eq;
using ::testing::Not;

constexpr absl::string_view kTableEntries = R"pb(
  entries {
    acl_pre_ingress_table_entry {
      match {
        in_port { value: "3" }
        src_mac { value: "22:22:22:11:11:11" mask: "ff:ff:ff:ff:ff:ff" }
        dst_ip { value: "10.0.10.0" mask: "255.255.255.255" }
      }
      action { set_vrf { vrf_id: "vrf-80" } }
      priority: 1
    }
  }
  entries {
    ipv4_table_entry {
      match { vrf_id: "vrf-80" }
      action { set_nexthop_id { nexthop_id: "nexthop-1" } }
    }
  }
  entries {
    l3_admit_table_entry {
      match {
        dst_mac { value: "66:55:44:33:22:10" mask: "ff:ff:ff:ff:ff:ff" }
        in_port { value: "5" }
      }
      action { admit_to_l3 {} }
      priority: 1
    }
  }
  entries {
    nexthop_table_entry {
      match { nexthop_id: "nexthop-1" }
      action {
        set_ip_nexthop {
          router_interface_id: "router-interface-1"
          neighbor_id: "fe80::cebb:aaff:fe99:8877"
        }
      }
    }
  }
  entries {
    router_interface_table_entry {
      match { router_interface_id: "router-interface-1" }
      action { set_port_and_src_mac { port: "2" src_mac: "66:55:44:33:22:11" } }
    }
  }
  entries {
    neighbor_table_entry {
      match {
        router_interface_id: "router-interface-1"
        neighbor_id: "fe80::cebb:aaff:fe99:8877"
      }
      action { set_dst_mac { dst_mac: "cc:bb:aa:99:88:77" } }
    }
  }
)pb";

constexpr absl::string_view kTableEntriesWithTunneling = R"pb(
  entries {
    acl_pre_ingress_table_entry {
      match { src_mac { value: "01:02:03:04:05:06" mask: "ff:ff:ff:ff:ff:ff" } }
      action { set_vrf { vrf_id: "vrf-1" } }
      priority: 1
    }
  }

  entries {
    ipv4_table_entry {
      match {
        vrf_id: "vrf-1"
        ipv4_dst { value: "10.0.0.5" prefix_length: 32 }
      }
      action {
        set_wcmp_group_id_and_metadata {
          wcmp_group_id: "group-1"
          route_metadata: "0x01"
        }
      }
    }
  }

  entries {
    wcmp_group_table_entry {
      match { wcmp_group_id: "group-1" }
      wcmp_actions {
        action { set_nexthop_id { nexthop_id: "nexthop-1" } }
        weight: 1
        watch_port: "2"
      }
      wcmp_actions {
        action { set_nexthop_id { nexthop_id: "nexthop-2" } }
        weight: 1
        watch_port: "4"
      }
    }
  }

  entries {
    nexthop_table_entry {
      match { nexthop_id: "nexthop-1" }
      action { set_p2p_tunnel_encap_nexthop { tunnel_id: "tunnel-1" } }
    }
  }

  entries {
    tunnel_table_entry {
      match { tunnel_id: "tunnel-1" }
      action {
        mark_for_p2p_tunnel_encap {
          encap_src_ip: "0001:0002:0003:0004::"
          encap_dst_ip: "0005:0006:0007:0008::"
          router_interface_id: "router-interface-1"
        }
      }
    }
  }

  entries {
    neighbor_table_entry {
      match {
        router_interface_id: "router-interface-1"
        neighbor_id: "fe80::508:09ff:fe0a:0b0c"
      }
      action { set_dst_mac { dst_mac: "07:08:09:0a:0b:0c" } }
    }
  }

  entries {
    router_interface_table_entry {
      match { router_interface_id: "router-interface-1" }
      action { set_port_and_src_mac { port: "2" src_mac: "0d:0e:0f:10:11:12" } }
    }
  }
)pb";

class P4SymbolicComponentTest : public testing::Test {
 public:
  thinkit::TestEnvironment& Environment() { return *environment_; }

 private:
  std::unique_ptr<thinkit::TestEnvironment> environment_ =
      absl::make_unique<thinkit::BazelTestEnvironment>(
          /*mask_known_failures=*/true);
};

absl::StatusOr<std::string> GenerateSmtForSaiPiplelineWithSimpleEntries() {
  const auto config = sai::GetNonstandardForwardingPipelineConfig(
      sai::Instantiation::kMiddleblock, sai::NonstandardPlatform::kP4Symbolic);
  ASSIGN_OR_RETURN(const pdpi::IrP4Info ir_p4info,
                   pdpi::CreateIrP4Info(config.p4info()));
  auto pd_entries = ParseProtoOrDie<sai::TableEntries>(kTableEntries);
  std::vector<p4::v1::TableEntry> pi_entries;
  for (auto& pd_entry : pd_entries.entries()) {
    ASSIGN_OR_RETURN(pi_entries.emplace_back(),
                     pdpi::PdTableEntryToPi(ir_p4info, pd_entry));
  }

  // TODO: a workaround for using global Z3 context.
  Z3Context(/*renew=*/true);

  ASSIGN_OR_RETURN(std::unique_ptr<symbolic::SolverState> solver,
                   EvaluateSaiPipeline(config, pi_entries));

  return solver->solver->to_smt2();
}

// Generate SMT constraints for the SAI pipeline from scratch multiple times and
// make sure the results remain the same.
TEST_F(P4SymbolicComponentTest,
       DISABLED_ConstraintGenerationIsDeterministicForSai) {
  constexpr int kNumberOfRuns = 5;
  ASSERT_OK_AND_ASSIGN(const std::string reference_smt_formula,
                       GenerateSmtForSaiPiplelineWithSimpleEntries());
  for (int run = 0; run < kNumberOfRuns; ++run) {
    LOG(INFO) << "Run " << run;
    ASSERT_OK_AND_ASSIGN(const std::string smt_formula,
                         GenerateSmtForSaiPiplelineWithSimpleEntries());
    ASSERT_THAT(smt_formula, Eq(reference_smt_formula));
  }
}

TEST_F(P4SymbolicComponentTest, CanGenerateTestPacketsForSimpleSaiP4Entries) {
  // Some constants.
  thinkit::TestEnvironment& env = Environment();
  const auto config = sai::GetNonstandardForwardingPipelineConfig(
      sai::Instantiation::kMiddleblock, sai::NonstandardPlatform::kP4Symbolic);
  ASSERT_OK_AND_ASSIGN(const pdpi::IrP4Info ir_p4info,
                       pdpi::CreateIrP4Info(config.p4info()));
  EXPECT_OK(env.StoreTestArtifact("ir_p4info.textproto", ir_p4info));
  EXPECT_OK(env.StoreTestArtifact("p4_config.json", config.p4_device_config()));

  // Prepare hard-coded table entries.
  auto pd_entries = ParseProtoOrDie<sai::TableEntries>(kTableEntries);
  EXPECT_OK(env.StoreTestArtifact("pd_entries.textproto", pd_entries));
  std::vector<p4::v1::TableEntry> pi_entries;
  for (auto& pd_entry : pd_entries.entries()) {
    ASSERT_OK_AND_ASSIGN(pi_entries.emplace_back(),
                         pdpi::PdTableEntryToPi(ir_p4info, pd_entry));
  }

  // Symbolically evaluate program.
  ASSERT_OK_AND_ASSIGN(
      symbolic::Dataplane dataplane,
      ParseToIr(config.p4_device_config(), ir_p4info, pi_entries));
  std::vector<int> ports = {1, 2, 3, 4, 5};
  symbolic::TranslationPerType translations;
  translations[kPortIdTypeName] = symbolic::values::TranslationData{
      .static_mapping = {{"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}},
      .dynamic_translation = false,
  };
  translations[kVrfIdTypeName] = symbolic::values::TranslationData{
      .static_mapping = {{"", 0}},
      .dynamic_translation = true,
  };
  LOG(INFO) << "building model (this may take a while) ...";
  absl::Time start_time = absl::Now();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<symbolic::SolverState> solver_state,
      symbolic::EvaluateP4Pipeline(dataplane, ports, translations));
  LOG(INFO) << "-> done in " << (absl::Now() - start_time);
  // Add constraints for parser.
  ASSERT_OK_AND_ASSIGN(
      std::vector<z3::expr> parser_constraints,
      EvaluateSaiParser(solver_state->context.ingress_headers));
  for (auto& constraint : parser_constraints) {
    solver_state->solver->add(constraint);
  }
  // Dump solver state.
  for (auto& [name, entries] : solver_state->entries) {
    std::string banner = absl::StrCat(
        "== ", name, " ", std::string(80 - name.size() - 4, '='), "\n");
    EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", banner));
    for (auto& entry : entries) {
      EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", entry));
    }
  }
  EXPECT_OK(env.StoreTestArtifact("program.textproto", solver_state->program));

  // Define assertion to hit IPv4 table entry, and solve for it.
  symbolic::Assertion hit_ipv4_table_entry =
      [](const symbolic::SymbolicContext& ctx) -> z3::expr {
    CHECK_NE(ctx.trace.matched_entries.count("ingress.routing.ipv4_table"), 0);
    auto ipv4_table =
        ctx.trace.matched_entries.at("ingress.routing.ipv4_table");
    return ipv4_table.matched && ipv4_table.entry_index == 0 &&
           !ctx.trace.dropped;
  };
  EXPECT_OK(env.StoreTestArtifact(
      "hit_ipv4_table_entry.smt",
      symbolic::DebugSMT(solver_state, hit_ipv4_table_entry)));
  ASSERT_OK_AND_ASSIGN(absl::optional<symbolic::ConcreteContext> solution,
                       symbolic::Solve(solver_state, hit_ipv4_table_entry));
  ASSERT_THAT(solution, Not(Eq(absl::nullopt)));
  EXPECT_OK(env.StoreTestArtifact("hit_ipv4_table_entry.solution.txt",
                                  solution->to_string(/*verbose=*/true)));

  // Check some properties of the solution.
  auto& ingress = solution->ingress_headers;
  auto& egress = solution->egress_headers;
  EXPECT_EQ(ingress["ethernet.ether_type"], "#x0800");
  // // TODO: p4-symbolic is flaky.
  if (!env.MaskKnownFailures()) {
    EXPECT_EQ(ingress["ethernet.eth_src"], "#x222222111111");
    EXPECT_EQ(ingress["ipv4.ipv4_dst"], "#x0a000a00");
  }
  EXPECT_EQ(egress["ethernet.ether_type"], "#x0800");
  EXPECT_EQ(egress["ethernet.dst_addr"], "#xccbbaa998877");
  EXPECT_EQ(egress["ethernet.src_addr"], "#x665544332211");

  // Make sure local_metadata.ingress_port is as expected.
  ASSERT_OK_AND_ASSIGN(const std::string local_metadata_ingress_port,
                       ExtractLocalMetadataIngressPortFromModel(*solver_state));
  ASSERT_THAT(local_metadata_ingress_port, Eq("3"));

  // Make sure standard_metadata.egress_port and standard_metadata.egress_spec
  // are as expected.
  EXPECT_EQ(Z3ValueStringToInt(egress["standard_metadata.egress_port"]), 2);
  EXPECT_EQ(Z3ValueStringToInt(egress["standard_metadata.egress_spec"]), 2);
}

TEST_F(P4SymbolicComponentTest,
       CanGenerateTestPacketsForSimpleSaiP4EntriesWithTunneling) {
  // Some constants.
  thinkit::TestEnvironment& env = Environment();
  const auto config = sai::GetNonstandardForwardingPipelineConfig(
      sai::Instantiation::kFabricBorderRouter,
      sai::NonstandardPlatform::kP4Symbolic);
  ASSERT_OK_AND_ASSIGN(const pdpi::IrP4Info ir_p4info,
                       pdpi::CreateIrP4Info(config.p4info()));
  EXPECT_OK(env.StoreTestArtifact("ir_p4info.textproto", ir_p4info));
  EXPECT_OK(env.StoreTestArtifact("p4_config.json", config.p4_device_config()));

  // Prepare hard-coded table entries.
  auto pd_entries =
      ParseProtoOrDie<sai::TableEntries>(kTableEntriesWithTunneling);
  EXPECT_OK(env.StoreTestArtifact("pd_entries.textproto", pd_entries));
  std::vector<p4::v1::TableEntry> pi_entries;
  for (auto& pd_entry : pd_entries.entries()) {
    ASSERT_OK_AND_ASSIGN(pi_entries.emplace_back(),
                         pdpi::PdTableEntryToPi(ir_p4info, pd_entry));
  }

  // Symbolically evaluate program.
  ASSERT_OK_AND_ASSIGN(
      symbolic::Dataplane dataplane,
      ParseToIr(config.p4_device_config(), ir_p4info, pi_entries));
  std::vector<int> ports = {1, 2, 3, 4, 5};
  symbolic::TranslationPerType translations;
  translations[kPortIdTypeName] = symbolic::values::TranslationData{
      .static_mapping = {{"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}},
      .dynamic_translation = false,
  };
  translations[kVrfIdTypeName] = symbolic::values::TranslationData{
      .static_mapping = {{"", 0}},
      .dynamic_translation = true,
  };
  LOG(INFO) << "building model (this may take a while) ...";
  absl::Time start_time = absl::Now();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<symbolic::SolverState> solver_state,
      symbolic::EvaluateP4Pipeline(dataplane, ports, translations));
  LOG(INFO) << "-> done in " << (absl::Now() - start_time);
  // Add constraints for parser.
  ASSERT_OK_AND_ASSIGN(
      std::vector<z3::expr> parser_constraints,
      EvaluateSaiParser(solver_state->context.ingress_headers));
  for (auto& constraint : parser_constraints) {
    solver_state->solver->add(constraint);
  }
  // Dump solver state.
  for (auto& [name, entries] : solver_state->entries) {
    std::string banner = absl::StrCat(
        "== ", name, " ", std::string(80 - name.size() - 4, '='), "\n");
    EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", banner));
    for (auto& entry : entries) {
      EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", entry));
    }
  }
  EXPECT_OK(env.StoreTestArtifact("program.textproto", solver_state->program));

  // Define assertion to hit the tunnel_table_entry.
  symbolic::Assertion hit_tunnel_table_entry =
      [](const symbolic::SymbolicContext& ctx) -> z3::expr {
    CHECK(ctx.trace.matched_entries.contains("ingress.routing.tunnel_table"));
    auto tunnel_table =
        ctx.trace.matched_entries.at("ingress.routing.tunnel_table");
    return tunnel_table.matched && tunnel_table.entry_index == 0 &&
           !ctx.trace.dropped;
  };
  EXPECT_OK(env.StoreTestArtifact(
      "hit_tunnel_table_entry.smt",
      symbolic::DebugSMT(solver_state, hit_tunnel_table_entry)));
  ASSERT_OK_AND_ASSIGN(absl::optional<symbolic::ConcreteContext> solution,
                       symbolic::Solve(solver_state, hit_tunnel_table_entry));
  ASSERT_THAT(solution, Not(Eq(absl::nullopt)));
  EXPECT_OK(env.StoreTestArtifact("hit_tunnel_table_entry.solution.txt",
                                  solution->to_string(/*verbose=*/true)));

  // Check some properties of the solution.
  auto& egress = solution->egress_headers;
  EXPECT_EQ(Z3ValueStringToInt(egress["tunnel_encap_gre.$valid$"]), 1);
  EXPECT_EQ(Z3ValueStringToInt(egress["standard_metadata.egress_port"]), 2);
  EXPECT_EQ(egress["tunnel_encap_ipv6.src_addr"],
            "#x00010002000300040000000000000000");  // "0001:0002:0003:0004::"
  EXPECT_EQ(egress["tunnel_encap_ipv6.dst_addr"],
            "#x00050006000700080000000000000000");  // "0005:0006:0007:0008::"
}

}  // namespace
}  // namespace p4_symbolic

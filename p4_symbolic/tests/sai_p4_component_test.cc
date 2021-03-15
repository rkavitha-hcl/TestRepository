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
#include "p4_symbolic/symbolic/symbolic.h"
#include "sai_p4/instantiations/google/sai_nonstandard_platforms.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "sai_p4/instantiations/google/switch_role.h"
#include "thinkit/bazel_test_environment.h"

namespace p4_symbolic {
namespace {

using ::gutil::ParseProtoOrDie;
using ::p4::config::v1::P4Info;
using ::testing::Eq;
using ::testing::Not;

constexpr absl::string_view kTableEntries = R"PB(
  entries {
    acl_lookup_table_entry {
      match {}
      action { set_vrf { vrf_id: "vrf-80" } }
      priority: 1
    }
  }
  entries {
    ipv6_table_entry {
      match { vrf_id: "vrf-80" }
      action { drop {} }
    }
  }
  entries {
    ipv4_table_entry {
      match { vrf_id: "vrf-80" }
      action { set_nexthop_id { nexthop_id: "nexthop-1" } }
    }
  }
  entries {
    nexthop_table_entry {
      match { nexthop_id: "nexthop-1" }
      action {
        set_nexthop {
          router_interface_id: "router-interface-1"
          neighbor_id: "neighbor-1"
        }
      }
    }
  }
  entries {
    router_interface_table_entry {
      match { router_interface_id: "router-interface-1" }
      action {
        set_port_and_src_mac { port: "0x002" src_mac: "66:55:44:33:22:11" }
      }
    }
  }
  entries {
    neighbor_table_entry {
      match {
        router_interface_id: "router-interface-1"
        neighbor_id: "neighbor-1"
      }
      action { set_dst_mac { dst_mac: "cc:bb:aa:99:88:77" } }
    }
  }
)PB";

class P4SymbolicComponentTest : public testing::Test {
 public:
  thinkit::TestEnvironment& Environment() { return *environment_; }

 private:
  std::unique_ptr<thinkit::TestEnvironment> environment_ =
      absl::make_unique<thinkit::BazelTestEnvironment>(
          /*mask_known_failures=*/true);
};

TEST_F(P4SymbolicComponentTest, CanGenerateTestPacketsForSimpleSaiP4Entries) {
  // Some constants.
  thinkit::TestEnvironment& env = Environment();
  const auto role = sai::SwitchRole::kMiddleblock;
  const auto platform = sai::NonstandardPlatform::kP4Symbolic;
  const P4Info p4info = sai::GetNonstandardP4Info(role, platform);
  ASSERT_OK_AND_ASSIGN(const pdpi::IrP4Info ir_p4info,
                       pdpi::CreateIrP4Info(p4info));
  const std::string bmv2_config = sai::GetNonstandardP4Config(role, platform);

  // Prepare hard-coded table entries.
  auto pd_entries = ParseProtoOrDie<sai::TableEntries>(kTableEntries);
  EXPECT_OK(env.StoreTestArtifact("pd_entries.textproto", pd_entries));
  std::vector<p4::v1::TableEntry> pi_entries;
  for (auto& pd_entry : pd_entries.entries()) {
    ASSERT_OK_AND_ASSIGN(pi_entries.emplace_back(),
                         pdpi::PdTableEntryToPi(ir_p4info, pd_entry));
  }

  // Prepare p4-symbolic.
  ASSERT_OK_AND_ASSIGN(symbolic::Dataplane dataplane,
                       ParseToIr(bmv2_config, ir_p4info, pi_entries));
  std::vector<int> ports = {0, 1, 2};
  LOG(INFO) << "building model (this may take a while) ...";
  absl::Time start_time = absl::Now();
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<symbolic::SolverState> solver_state,
      symbolic::EvaluateP4Pipeline(dataplane, ports,
                                   /*hardcoded_parser=*/false));
  LOG(INFO) << "-> done in " << (absl::Now() - start_time);
  for (auto& [name, entries] : solver_state->entries) {
    std::string banner = absl::StrCat(
        "== ", name, " ", std::string(80 - name.size() - 4, '='), "\n");
    EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", banner));
    for (auto& entry : entries) {
      EXPECT_OK(env.AppendToTestArtifact("ir_entries.textproto", entry));
    }
  }
  EXPECT_OK(env.StoreTestArtifact("program.textproto", solver_state->program));

  // Define assertion to hit IPv4 table entry.
  symbolic::Assertion hit_ipv4_table_entry =
      [](const symbolic::SymbolicContext& ctx) -> z3::expr {
    CHECK_NE(ctx.trace.matched_entries.count("ingress.routing.ipv4_table"), 0);
    auto ipv4_table =
        ctx.trace.matched_entries.at("ingress.routing.ipv4_table");
    return ipv4_table.matched && ipv4_table.entry_index == 0;
  };
  EXPECT_OK(env.StoreTestArtifact(
      "hit_ipv4_table_entry.smt",
      symbolic::DebugSMT(solver_state, hit_ipv4_table_entry)));
  ASSERT_OK_AND_ASSIGN(
      absl::optional<symbolic::ConcreteContext> concrete_context,
      symbolic::Solve(solver_state, hit_ipv4_table_entry));
  ASSERT_THAT(concrete_context, Not(Eq(absl::nullopt)));
  EXPECT_OK(
      env.StoreTestArtifact("hit_ipv4_table_entry.solution.txt",
                            concrete_context->to_string(/*verbose=*/true)));
}

}  // namespace
}  // namespace p4_symbolic

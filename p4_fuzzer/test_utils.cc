#include "p4_fuzzer/test_utils.h"

#include "absl/random/random.h"
#include "absl/strings/substitute.h"
#include "gutil/testing.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer_config.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/ir.h"

namespace p4_fuzzer {
absl::StatusOr<pdpi::IrP4Info> ConstructIrInfo(
    const TestP4InfoOptions& options) {
  return pdpi::CreateIrP4Info(
      gutil::ParseProtoOrDie<p4::config::v1::P4Info>(absl::Substitute(
          R"pb(
            tables {
              preamble {
                id: $0
                name: "ingress.routing.wcmp_group_table"
                alias: "wcmp_group_table"
                annotations: "@p4runtime_role(\"sdn_controller\")"
                annotations: "@oneshot"
              }
              match_fields { id: 1 name: "wcmp_group_id" match_type: EXACT }
              action_refs { id: $1 annotations: "@proto_id(1)" }
              action_refs {
                id: $2
                annotations: "@defaultonly"
                scope: DEFAULT_ONLY
              }
              const_default_action_id: $2
              implementation_id: $3
              size: 4096
            }
            actions {
              preamble {
                id: $1
                name: "ingress.routing.set_nexthop_id"
                alias: "set_nexthop_id"
              }
              params { id: 1 name: "nexthop_id" }
            }
            actions { preamble { id: $2 name: "NoAction" alias: "NoAction" } }
            action_profiles {
              preamble {
                id: $3
                name: "ingress.routing.wcmp_group_selector"
                alias: "wcmp_group_selector"
              }
              table_ids: $0
              with_selector: true
              size: $4
              max_group_size: $5
            }
          )pb",
          options.action_selector_table_id, options.action_id,
          options.action_no_op_id, options.action_profile_id,
          options.action_profile_size, options.action_profile_max_group_size)));
}

absl::StatusOr<FuzzerTestState> ConstructFuzzerTestState(
    const TestP4InfoOptions& options) {
  ASSIGN_OR_RETURN(const pdpi::IrP4Info ir_info, ConstructIrInfo(options));
  const FuzzerConfig config{
      .info = ir_info,
      .ports = {"1"},
      .qos_queues = {"0x1"},
      .role = "sdn_controller",
  };
  return FuzzerTestState{
      .config = config,
      .switch_state = SwitchState(ir_info),
  };
}
}  // namespace p4_fuzzer

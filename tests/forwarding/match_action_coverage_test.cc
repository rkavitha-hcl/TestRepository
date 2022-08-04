#include "tests/forwarding/match_action_coverage_test.h"

#include <bitset>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/random/random.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/collections.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_constraints/backend/constraint_info.h"
#include "p4_fuzzer/fuzz_util.h"
#include "p4_fuzzer/fuzzer_config.h"
#include "p4_fuzzer/switch_state.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/pd.h"
#include "p4_pdpi/string_encodings/hex_string.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/lib/switch_test_setup_helpers.h"
#include "thinkit/mirror_testbed.h"

namespace p4_fuzzer {
namespace {

using ::p4::v1::TableEntry;
using ::p4::v1::Update;

using TableEntryPredicate = std::function<bool(const TableEntry&)>;

bool EntryUsesMatchField(
    const TableEntry& entry,
    const pdpi::IrMatchFieldDefinition& match_field_definition) {
  for (const auto& match : entry.match()) {
    if (match.field_id() == match_field_definition.match_field().id()) {
      return true;
    }
  }
  return false;
}

bool EntryUsesAction(const TableEntry& entry,
                     const pdpi::IrActionReference& action_reference) {
  if (entry.action().has_action()) {
    return entry.action().action().action_id() ==
           action_reference.action().preamble().id();
  }
  if (entry.action().has_action_profile_action_set()) {
    for (const auto& action :
         entry.action().action_profile_action_set().action_profile_actions()) {
      if (action.action().action_id() ==
          action_reference.action().preamble().id()) {
        return true;
      }
    }
  }
  return false;
}

bool IsOmittable(const pdpi::IrMatchFieldDefinition& match_field_definition) {
  return match_field_definition.match_field().match_type() ==
             p4::config::v1::MatchField::TERNARY ||
         match_field_definition.match_field().match_type() ==
             p4::config::v1::MatchField::OPTIONAL ||
         match_field_definition.match_field().match_type() ==
             p4::config::v1::MatchField::LPM;
}

absl::StatusOr<int> ExtractRouteMetadataAsIntFromEntry(
    const sai::TableEntry& pd_entry) {
  if (pd_entry.has_ipv4_table_entry()) {
    const sai::Ipv4TableEntry::Action action =
        pd_entry.ipv4_table_entry().action();
    if (action.has_set_metadata_and_drop()) {
      return pdpi::HexStringToInt(
          action.set_metadata_and_drop().route_metadata());
    }
    if (action.has_set_wcmp_group_id_and_metadata()) {
      return pdpi::HexStringToInt(
          action.set_wcmp_group_id_and_metadata().route_metadata());
    }
    if (action.has_set_nexthop_id_and_metadata()) {
      return pdpi::HexStringToInt(
          action.set_nexthop_id_and_metadata().route_metadata());
    }
  }
  if (pd_entry.has_ipv6_table_entry()) {
    const sai::Ipv6TableEntry::Action action =
        pd_entry.ipv6_table_entry().action();
    if (action.has_set_metadata_and_drop()) {
      return pdpi::HexStringToInt(
          action.set_metadata_and_drop().route_metadata());
    }
    if (action.has_set_wcmp_group_id_and_metadata()) {
      return pdpi::HexStringToInt(
          action.set_wcmp_group_id_and_metadata().route_metadata());
    }
    if (action.has_set_nexthop_id_and_metadata()) {
      return pdpi::HexStringToInt(
          action.set_nexthop_id_and_metadata().route_metadata());
    }
  }
  return gutil::InvalidArgumentErrorBuilder()
         << "expected entry with route metadata, but got:"
         << pd_entry.DebugString();
}

// Used to mask bugs that affect specific entries, but not every entry of a
// particular table, with a particular match field, or with a particular action.
// Such bugs should be masked using the FuzzerConfig's
// `disabled_fully_qualified_names`.
bool EntryTriggersKnownBug(const pdpi::IrP4Info& info,
                           const TableEntry& entry) {
  // Convert the entry to PD to simplify logic.
  sai::TableEntry pd_entry;
  CHECK(pdpi::PiTableEntryToPd(info, entry, &pd_entry).ok());  // Crash OK

  if (auto route_metadata = ExtractRouteMetadataAsIntFromEntry(pd_entry);
      route_metadata.ok()) {
    // Route metadata values between 56 and 63 are reserved for internal switch
    // operations.
    // TODO: This should be handled by P4 Constraints.
    return *route_metadata >= 56;
  }
  return false;
}

absl::Status ModifyEntryToAvoidKnownBug(const pdpi::IrP4Info& info,
                                        TableEntry& entry) {
  // Convert the entry to PD to simplify logic.
  sai::TableEntry pd_entry;
  RETURN_IF_ERROR(pdpi::PiTableEntryToPd(info, entry, &pd_entry));

  // TODO: Remove once this is covered by P4 Constraints.
  if (pd_entry.has_l3_admit_table_entry()) {
    pd_entry.mutable_l3_admit_table_entry()->set_priority(1);
  }
  ASSIGN_OR_RETURN(entry, pdpi::PdTableEntryToPi(info, pd_entry));
  return absl::OkStatus();
}

// Generates valid table entries for `table` until one meets the given
// `predicate`. Unless an entry with the same keys already exists on the switch,
// installs the generated table entry and updates `state`.
absl::Status GenerateAndInstallEntryThatMeetsPredicate(
    absl::BitGen& gen, pdpi::P4RuntimeSession& session,
    const FuzzerConfig& config, SwitchState& state,
    thinkit::TestEnvironment& environment, const pdpi::IrTableDefinition& table,
    const TableEntryPredicate& predicate) {
  TableEntry entry;
  // Generate new entries until we satisfy our predicates or do not trigger
  // known bugs (if we are masking known failures). Continue trying for at most
  // 10 seconds.
  const absl::Time kDeadline = absl::Now() + absl::Seconds(10);
  do {
    ASSIGN_OR_RETURN(entry, FuzzValidTableEntry(&gen, config, state, table));
    if (environment.MaskKnownFailures()) {
      RETURN_IF_ERROR(ModifyEntryToAvoidKnownBug(config.info, entry));
    }
  } while ((!predicate(entry) || (environment.MaskKnownFailures() &&
                                  EntryTriggersKnownBug(config.info, entry))) &&
           absl::Now() < kDeadline);
  // If we timeout, we return an error.
  if (!predicate(entry)) {
    return gutil::DeadlineExceededErrorBuilder() << absl::Substitute(
               "failed to generate an entry meeting the given predicate for "
               "table '$0' within 10s",
               table.preamble().alias());
  }

  // If the generated table entry is identical to one we already have installed,
  // then we return early since we have already covered the predicate.
  if (state.GetTableEntry(entry).has_value()) return absl::OkStatus();

  sai::TableEntry pd_entry;
  RETURN_IF_ERROR(pdpi::PiTableEntryToPd(config.info, entry, &pd_entry));
  RETURN_IF_ERROR(environment.AppendToTestArtifact(
      "requests_and_responses.txt",
      absl::StrCat("# PD Table Entry:\n", pd_entry.DebugString())));

  if (absl::Status status = pdpi::InstallPiTableEntry(&session, entry);
      status.ok()) {
    RETURN_IF_ERROR(environment.AppendToTestArtifact(
        "requests_and_responses.txt", "# Successfully installed!\n"));
  } else {
    RETURN_IF_ERROR(environment.AppendToTestArtifact(
        "requests_and_responses.txt",
        absl::StrCat("# Installation failed:\n", status.ToString())));
    RETURN_IF_ERROR(status) << "\nPD entry:\n" << pd_entry.DebugString();
  }

  // Update state.
  Update update;
  update.set_type(Update::INSERT);
  // TODO: The switch does not currently recognize the `priority`
  // as being part of a table entry's unique identifier. Stop clearing the
  // priority once that is fixed.
  entry.clear_priority();
  *update.mutable_entity()->mutable_table_entry() = entry;
  return state.ApplyUpdate(update);
}

// For each programmable table, installs a set of table entries covering all
// match fields and actions in the following sense:
// - Each omittable match field is omitted and included in at least one table
//   entry.
// - Each action is included in at least one table entry.
absl::Status AddTableEntryForEachMatchAndEachAction(
    absl::BitGen& gen, pdpi::P4RuntimeSession& session,
    const FuzzerConfig& config, SwitchState& state,
    thinkit::TestEnvironment& environment,
    const p4::config::v1::P4Info& p4info) {
  ASSIGN_OR_RETURN(const p4_constraints::ConstraintInfo constraints_by_table_id,
                   p4_constraints::P4ToConstraintInfo(p4info));

  for (uint32_t table_id : AllValidTablesForP4RtRole(config)) {
    ASSIGN_OR_RETURN(const pdpi::IrTableDefinition& table,
                     gutil::FindOrStatus(config.info.tables_by_id(), table_id));

    // TODO: Stop skipping tables that have constraints once the
    // fuzzer supports them.
    if (constraints_by_table_id.at(table_id).constraint.has_value()) {
      LOG(WARNING) << absl::Substitute(
          "No entries installed into table '$0' due to use of constraints, "
          "which are not yet supported by the Fuzzer.",
          table.preamble().alias());
      continue;
    }

    LOG(INFO) << absl::Substitute("For table '$0', installing entries with:",
                                  table.preamble().alias());
    std::vector<std::string> required_match_descriptions;
    std::vector<std::string> omittable_match_descriptions;
    // For each valid match field, install a table entry with (and without
    // if possible) that field.
    for (const pdpi::IrMatchFieldDefinition& field :
         AllValidMatchFields(config, table)) {
      if (!IsOmittable(field)) {
        // If the field can't be a wildcard, then any value will do:
        RETURN_IF_ERROR(GenerateAndInstallEntryThatMeetsPredicate(
                            gen, session, config, state, environment, table,
                            /*predicate=*/[](auto&) { return true; }))
                .SetPrepend()
            << absl::Substitute("while generating entry with field $0: ",
                                field.match_field().name());
        required_match_descriptions.push_back(
            absl::Substitute("   -   $0: Present", field.match_field().name()));
      } else {
        // If the field can be a wildcard, install one entry with a wildcard and
        // one without.
        for (bool use_field : {true, false}) {
          RETURN_IF_ERROR(GenerateAndInstallEntryThatMeetsPredicate(
                              gen, session, config, state, environment, table,
                              /*predicate=*/
                              [&](const TableEntry& entry) {
                                return use_field ==
                                       EntryUsesMatchField(entry, field);
                              }))
                  .SetPrepend()
              << absl::Substitute("while generating entry $0 field $1: ",
                                  (use_field ? "with" : "without"),
                                  field.match_field().name());
        }
        omittable_match_descriptions.push_back(absl::Substitute(
            "   -   $0: Present and Absent", field.match_field().name()));
      }
    }

    // Log whether we hit the match fields in the table.
    if (!required_match_descriptions.empty()) {
      LOG(INFO) << "-  Required match fields:";
      for (const std::string& description : required_match_descriptions) {
        LOG(INFO) << description;
      }
    }

    // Only omittable match fields can be disabled.
    bool has_a_disabled_fully_qualified_name = false;
    for (const std::string& path : config.disabled_fully_qualified_names) {
      has_a_disabled_fully_qualified_name =
          has_a_disabled_fully_qualified_name ||
          absl::StartsWith(path, table.preamble().name());
    }
    if (!omittable_match_descriptions.empty() ||
        has_a_disabled_fully_qualified_name) {
      LOG(INFO) << "-  Omittable match fields:";
      for (const std::string& description : omittable_match_descriptions) {
        LOG(INFO) << description;
      }
      for (const std::string& path : config.disabled_fully_qualified_names) {
        if (absl::StartsWith(path, table.preamble().name())) {
          LOG(INFO) << absl::Substitute(
              "   -  $0: Absent due to being disabled",
              absl::StripPrefix(path, table.preamble().name()));
        }
      }
    }

    // For each valid action, install a table entry using it.
    LOG(INFO) << "-  Actions:";
    for (const pdpi::IrActionReference& action_reference :
         AllValidActions(config, table)) {
      RETURN_IF_ERROR(GenerateAndInstallEntryThatMeetsPredicate(
                          gen, session, config, state, environment, table,
                          /*predicate=*/
                          [&](const TableEntry& entry) {
                            return EntryUsesAction(entry, action_reference);
                          }))
              .SetPrepend()
          << absl::Substitute("while generating entry with action $0: ",
                              action_reference.action().preamble().alias());
      LOG(INFO) << absl::Substitute(
          "   -  $0: Present", action_reference.action().preamble().alias());
    }
    // Print disabled actions.
    for (const pdpi::IrActionReference& action_reference :
         table.entry_actions()) {
      if (config.disabled_fully_qualified_names.contains(
              action_reference.action().preamble().name())) {
        LOG(INFO) << absl::Substitute(
            "   -  $0: Absent due to being disabled",
            action_reference.action().preamble().alias());
      }
    }
  }

  // Print disabled tables.
  for (const auto& [name, table] : config.info.tables_by_name()) {
    if (config.disabled_fully_qualified_names.contains(
            table.preamble().name())) {
      LOG(INFO) << absl::Substitute(
          "No entries installed into table '$0' because it was disabled.",
          table.preamble().alias());
    }
  }
  return absl::OkStatus();
}

// Installs a table entry per supported, programmable table in a particular
// order. This ensures that we can generate valid table entries for every table.
// It is required due to the possibility of references between tables.
// TODO: This currently skips tables that use constraints. Don't do
// this once they are supported.
// TODO: Ideally, this function would use the p4info to extract a list of
// all tables, ordered such that every table only depends on (i.e. @refers_to)
// those before it, instead of using our hardcoded, ordered list.
absl::Status AddAuxiliaryTableEntries(absl::BitGen& gen,
                                      pdpi::P4RuntimeSession& session,
                                      const FuzzerConfig& config,
                                      SwitchState& state,
                                      thinkit::TestEnvironment& environment) {
  std::vector<std::string> kOrderedTablesToInsertEntriesInto = {
      "mirror_session_table",
      "l3_admit_table",
      "vrf_table",
      "router_interface_table",
      "neighbor_table",
      "tunnel_table",
      "nexthop_table",
      "wcmp_group_table",
      "ipv4_table",
      "ipv6_table",
  };

  for (const auto& table_name : kOrderedTablesToInsertEntriesInto) {
    LOG(INFO) << absl::StrCat("Adding auxiliary entry to ", table_name);
    ASSIGN_OR_RETURN(
        const pdpi::IrTableDefinition& table,
        gutil::FindOrStatus(config.info.tables_by_name(), table_name));
    RETURN_IF_ERROR(GenerateAndInstallEntryThatMeetsPredicate(
                        gen, session, config, state, environment, table,
                        /*predicate=*/[](auto&) { return true; }))
            .SetPrepend()
        << "while trying to generate entry for '" << table_name << "': ";
  }
  return absl::OkStatus();
}

TEST_P(MatchActionCoverageTestFixture,
       InsertEntriesForEveryTableAndMatchConfiguration) {
  thinkit::MirrorTestbed& testbed =
      GetParam().testbed_interface->GetMirrorTestbed();

  // Initialize the connection and clear table entries for the SUT.
  ASSERT_OK_AND_ASSIGN(
      std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session,
      pins_test::ConfigureSwitchAndReturnP4RuntimeSession(
          testbed.Sut(), /*gnmi_config=*/std::nullopt, GetParam().p4info));

  absl::BitGen gen;

  FuzzerConfig config = GetParam().config;
  // If the ports in the FuzzerConfig are unset, get all valid ports from the
  // switch via gNMI.
  if (config.ports.empty()) {
    ASSERT_OK_AND_ASSIGN(auto stub, testbed.Sut().CreateGnmiStub());
    ASSERT_OK_AND_ASSIGN(const absl::btree_set<std::string> ports,
                         pins_test::GetAllPortIds(*stub));
    config.ports = std::vector<std::string>(ports.begin(), ports.end());
  }

  ASSERT_FALSE(config.ports.empty())
      << "ports must be specified in the gNMI config pushed to the switch, but "
         "none were";

  // For this test, mutations are undesirable.
  config.mutate_update_probability = 0;

  SwitchState state(config.info);
  // Sets up the switch such that there is a possible valid entry per table.
  // This is required due to the possibility of references between tables.
  ASSERT_OK(AddAuxiliaryTableEntries(gen, *p4rt_session, config, state,
                                     testbed.Environment()));

  // Generates and installs entries that use every match field and action.
  EXPECT_OK(AddTableEntryForEachMatchAndEachAction(gen, *p4rt_session, config,
                                                   state, testbed.Environment(),
                                                   GetParam().p4info));
}

}  // namespace
}  // namespace p4_fuzzer

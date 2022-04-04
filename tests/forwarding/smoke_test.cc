// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tests/forwarding/smoke_test.h"

#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "lib/gnmi/gnmi_helper.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/test_data.h"
#include "tests/lib/p4rt_fixed_table_programming_helper.h"

namespace pins_test {
namespace {

using ::gutil::StatusIs;

TEST_P(SmokeTestFixture, SessionsAreNonNull) {
  ASSERT_NE(&GetSutP4RuntimeSession(), nullptr);
  ASSERT_NE(&GetControlP4RuntimeSession(), nullptr);
}

TEST_P(SmokeTestFixture, AclTableAddDeleteOkButModifyFails) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "3b18d5dc-3881-42a5-b667-d2ca0362ab3a");
  const sai::WriteRequest pd_insert = gutil::ParseProtoOrDie<sai::WriteRequest>(
      R"pb(
        updates {
          type: INSERT
          table_entry {
            acl_ingress_table_entry {
              match { is_ip { value: "0x1" } }
              priority: 10
              action { acl_copy { qos_queue: "0x1" } }
            }
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest pi_insert,
                       pdpi::PdWriteRequestToPi(GetIrP4Info(), pd_insert));

  const sai::WriteRequest pd_modify = gutil::ParseProtoOrDie<sai::WriteRequest>(
      R"pb(
        updates {
          type: MODIFY
          table_entry {
            acl_ingress_table_entry {
              match { is_ip { value: "0x1" } }
              priority: 10
              action { acl_forward {} }
            }
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest pi_modify,
                       pdpi::PdWriteRequestToPi(GetIrP4Info(), pd_modify));

  const sai::WriteRequest pd_delete = gutil::ParseProtoOrDie<sai::WriteRequest>(
      R"pb(
        updates {
          type: DELETE
          table_entry {
            acl_ingress_table_entry {
              match { is_ip { value: "0x1" } }
              priority: 10
              action { acl_forward {} }
            }
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest pi_delete,
                       pdpi::PdWriteRequestToPi(GetIrP4Info(), pd_delete));

  // Insert works.
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_insert));

  // ACL table entries are expected to contain counter data. However, it's
  // updated periodically and may not be avaialable immediatly after writing so
  // we poll the entry for a few seconds until we see the data.
  absl::Time timeout = absl::Now() + absl::Seconds(11);
  p4::v1::ReadResponse pi_read_response;
  p4::v1::ReadRequest pi_read_request;
  pi_read_request.add_entities()->mutable_table_entry();
  do {
    ASSERT_OK_AND_ASSIGN(pi_read_response,
                         pdpi::SetMetadataAndSendPiReadRequest(
                             &GetSutP4RuntimeSession(), pi_read_request));
    ASSERT_EQ(pi_read_response.entities_size(), 1);

    if (absl::Now() > timeout) {
      FAIL() << "ACL table entry does not have counter data.";
    }
  } while (!pi_read_response.entities(0).table_entry().has_counter_data());

  // Modify fails due to b/211916308.
  // Many ACL table attribues are NOT modifiable currently due to missing SAI
  // implementation. Because there are no production use-cases, these are
  // de-prioritized.
  ASSERT_THAT(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                     pi_modify),
              StatusIs(absl::StatusCode::kUnknown));

  // Delete works.
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_delete));
}

TEST_P(SmokeTestFixture, FixedTableAddModifyDeleteOk) {
  p4::v1::WriteRequest pi_request;
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::VrfTableUpdate(GetIrP4Info(), p4::v1::Update::INSERT, "vrf-1"));
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::RouterInterfaceTableUpdate(GetIrP4Info(), p4::v1::Update::INSERT,
                                        "router-intf-1", /*port=*/"1",
                                        /*src_mac=*/"00:01:02:03:04:05"));
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::NeighborTableUpdate(GetIrP4Info(), p4::v1::Update::INSERT,
                                 "router-intf-1",
                                 /*neighbor_id=*/"fe80::0000:00ff:fe17:5f80",
                                 /*dst_mac=*/"00:01:02:03:04:06"));
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::NexthopTableUpdate(GetIrP4Info(), p4::v1::Update::INSERT,
                                "nexthop-1", "router-intf-1",
                                /*neighbor_id=*/"fe80::0000:00ff:fe17:5f80"));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_request));

  // Add and modify IPV4 table entry with different number of action params.
  pi_request.Clear();
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::Ipv4TableUpdate(
          GetIrP4Info(), p4::v1::Update::INSERT,
          gpins::IpTableOptions{
              .vrf_id = "vrf-1",
              .dst_addr_lpm = std::make_pair("20.0.0.1", 32),
              .action = gpins::IpTableOptions::Action::kSetNextHopId,
              .nexthop_id = "nexthop-1",
          }));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_request));
  pi_request.Clear();
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::Ipv4TableUpdate(GetIrP4Info(), p4::v1::Update::MODIFY,
                             gpins::IpTableOptions{
                                 .vrf_id = "vrf-1",
                                 .dst_addr_lpm = std::make_pair("20.0.0.1", 32),
                                 .action = gpins::IpTableOptions::Action::kDrop,
                             }));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_request));

  pi_request.Clear();
  ASSERT_OK_AND_ASSIGN(
      *pi_request.add_updates(),
      gpins::Ipv4TableUpdate(GetIrP4Info(), p4::v1::Update::DELETE,
                             gpins::IpTableOptions{
                                 .vrf_id = "vrf-1",
                                 .dst_addr_lpm = std::make_pair("20.0.0.1", 32),
                                 .action = gpins::IpTableOptions::Action::kDrop,
                             }));
  ASSERT_OK(pdpi::SetMetadataAndSendPiWriteRequest(&GetSutP4RuntimeSession(),
                                                   pi_request));

  // This used to fail with a read error, see b/185508142.
  ASSERT_OK(pdpi::ClearTableEntries(&GetSutP4RuntimeSession()));
}

// TODO: Enable once the bug is fixed.
TEST_P(SmokeTestFixture, DISABLED_Bug181149419) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "e6ba12b7-18e0-4681-9562-87e2fc01d429");
  // Adding 8 mirror sessions should succeed.
  for (int i = 0; i < 8; i++) {
    sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
        R"pb(
          mirror_session_table_entry {
            match { mirror_session_id: "session" }
            action {
              mirror_as_ipv4_erspan {
                port: "1"
                src_ip: "10.206.196.0"
                dst_ip: "172.20.0.202"
                src_mac: "00:02:03:04:05:06"
                dst_mac: "00:1a:11:17:5f:80"
                ttl: "0x40"
                tos: "0x00"
              }
            }
          }
        )pb");
    pd_entry.mutable_mirror_session_table_entry()
        ->mutable_match()
        ->set_mirror_session_id(absl::StrCat("session-", i));

    ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                         pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));
    EXPECT_OK(pdpi::InstallPiTableEntry(&GetSutP4RuntimeSession(), pi_entry));
  }
  // Adding one entry above the limit will fail.
  {
    sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
        R"pb(
          mirror_session_table_entry {
            match { mirror_session_id: "session-9" }
            action {
              mirror_as_ipv4_erspan {
                port: "1"
                src_ip: "10.206.196.0"
                dst_ip: "172.20.0.202"
                src_mac: "00:02:03:04:05:06"
                dst_mac: "00:1a:11:17:5f:80"
                ttl: "0x40"
                tos: "0x00"
              }
            }
          }
        )pb");

    ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                         pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));
    EXPECT_FALSE(
        pdpi::InstallPiTableEntry(&GetSutP4RuntimeSession(), pi_entry).ok());
  }
  // Adding ACL entries that use the 8 mirrors should all succeed.
  for (int i = 0; i < 8; i++) {
    sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
        R"pb(
          acl_ingress_table_entry {
            match {
              is_ipv4 { value: "0x1" }
              src_ip { value: "10.0.0.0" mask: "255.255.255.255" }
              dscp { value: "0x1c" mask: "0x3c" }
            }
            action { mirror { mirror_session_id: "session-1" } }
            priority: 2100
          }
        )pb");
    pd_entry.mutable_acl_ingress_table_entry()
        ->mutable_action()
        ->mutable_acl_mirror()
        ->set_mirror_session_id(absl::StrCat("session-", i));
    pd_entry.mutable_acl_ingress_table_entry()
        ->mutable_match()
        ->mutable_src_ip()
        ->set_value(absl::StrCat("10.0.0.", i));

    ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                         pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));
    ASSERT_OK(pdpi::InstallPiTableEntry(&GetSutP4RuntimeSession(), pi_entry));
  }
}

TEST_P(SmokeTestFixture, InsertTableEntry) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "da103fbb-8fd4-4385-b997-34e12a41004b");
  const sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
      R"pb(
        router_interface_table_entry {
          match { router_interface_id: "router-interface-1" }
          action {
            set_port_and_src_mac { port: "1" src_mac: "02:2a:10:00:00:03" }
          }
        }
      )pb");

  ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                       pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));
  ASSERT_OK(pdpi::InstallPiTableEntry(&GetSutP4RuntimeSession(), pi_entry));
}

TEST_P(SmokeTestFixture, InsertTableEntryWithRandomCharacterId) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "bd22f5fe-4103-4729-91d0-cb2bc8258940");
  sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
      R"pb(
        router_interface_table_entry {
          match { router_interface_id: "\x01\x33\x00\xff,\":'}(*{+-" }
          action {
            set_port_and_src_mac { port: "1" src_mac: "02:2a:10:00:00:03" }
          }
        }
      )pb");

  ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                       pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));
  ASSERT_OK(pdpi::InstallPiTableEntry(&GetSutP4RuntimeSession(), pi_entry));
  ASSERT_OK_AND_ASSIGN(auto entries,
                       pdpi::ReadPiTableEntries(&GetSutP4RuntimeSession()));
  ASSERT_EQ(entries.size(), 1);
  ASSERT_THAT(entries[0], gutil::EqualsProto(pi_entry));
}

TEST_P(SmokeTestFixture, InsertAndReadTableEntries) {
  GetMirrorTestbed().Environment().SetTestCaseID(
      "8bdacde4-b261-4242-b65d-462c828a427d");
  pdpi::P4RuntimeSession& session = GetSutP4RuntimeSession();
  const pdpi::IrP4Info& ir_p4info = GetIrP4Info();
  std::vector<sai::TableEntry> write_pd_entries =
      sai_pd::CreateUpTo255GenericTableEntries(3);

  thinkit::TestEnvironment& test_environment = GetMirrorTestbed().Environment();
  std::vector<p4::v1::TableEntry> write_pi_entries;
  p4::v1::ReadResponse expected_read_response;
  write_pi_entries.reserve(write_pd_entries.size());
  for (const auto& pd_entry : write_pd_entries) {
    ASSERT_OK_AND_ASSIGN(p4::v1::TableEntry pi_entry,
                         pdpi::PdTableEntryToPi(ir_p4info, pd_entry));

    ASSERT_OK(test_environment.AppendToTestArtifact(
        "pi_entries_written.pb.txt",
        absl::StrCat(pi_entry.DebugString(), "\n")));
    *expected_read_response.add_entities()->mutable_table_entry() = pi_entry;
    write_pi_entries.push_back(std::move(pi_entry));
  }

  ASSERT_OK(pdpi::InstallPiTableEntries(&session, ir_p4info, write_pi_entries));

  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(
      p4::v1::ReadResponse read_response,
      pdpi::SetMetadataAndSendPiReadRequest(&session, read_request));

  for (const auto& entity : read_response.entities()) {
    ASSERT_OK(test_environment.AppendToTestArtifact(
        "pi_entries_read_back.pb.txt",
        absl::StrCat(entity.table_entry().DebugString(), "\n")));
  }

  // Compare the result in proto format since the fields being compared are
  // nested and out of order. Also ignore any dynamic fields (e.g. counters).
  google::protobuf::util::MessageDifferencer diff;
  diff.set_repeated_field_comparison(
      google::protobuf::util::MessageDifferencer::RepeatedFieldComparison::
          AS_SET);
  diff.IgnoreField(
      p4::v1::TableEntry::descriptor()->FindFieldByName("counter_data"));
  EXPECT_TRUE(diff.Compare(read_response, expected_read_response))
      << "Expected: " << expected_read_response.DebugString()
      << "\nActual: " << read_response.DebugString();
}

// Ensures that both CreateWithP4InfoAndClearTables and ClearTableEntries
// properly clear the table entries of a table.
TEST_P(SmokeTestFixture, EnsureClearTables) {
  // Sets up initial session.
  ASSERT_OK_AND_ASSIGN(auto session,
                       pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
                           GetMirrorTestbed().Sut(), GetP4Info()));
  // The table should be clear after setup.
  ASSERT_OK(pdpi::CheckNoTableEntries(session.get()));

  // Sets up an example table entry.
  const sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
      R"pb(
        router_interface_table_entry {
          match { router_interface_id: "router-interface-1" }
          action {
            set_port_and_src_mac { port: "1" src_mac: "02:2a:10:00:00:03" }
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(p4::v1::TableEntry pi_entry,
                       pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));

  ASSERT_OK(
      pdpi::InstallPiTableEntries(session.get(), GetIrP4Info(), {pi_entry}));

  ASSERT_OK(pdpi::ClearTableEntries(session.get()));
  // The table should be clear after clearing.
  ASSERT_OK(pdpi::CheckNoTableEntries(session.get()));

  ASSERT_OK(
      pdpi::InstallPiTableEntries(session.get(), GetIrP4Info(), {pi_entry}));

  ASSERT_OK_AND_ASSIGN(auto session2,
                       pdpi::P4RuntimeSession::CreateWithP4InfoAndClearTables(
                           GetMirrorTestbed().Sut(), GetP4Info()));

  // The table should be clear for both sessions after setting up a new session.
  ASSERT_OK(pdpi::CheckNoTableEntries(session.get()));
  ASSERT_OK(pdpi::CheckNoTableEntries(session2.get()));
}

// TODO: Enable the test once the bug is unblocked, indicating that
// the functionality is in place.
// Ensures that a GNMI Config can be pushed even with programmed flows already
// on the switch.
TEST_P(SmokeTestFixture, DISABLED_PushGnmiConfigWithFlows) {
  // All tables should be clear after setup.
  ASSERT_OK(pdpi::CheckNoTableEntries(&GetSutP4RuntimeSession()));

  const std::string gnmi_config = GetGnmiConfig();

  // Pushing a Gnmi Config is OK when tables are cleared.
  ASSERT_OK(pins_test::PushGnmiConfig(GetMirrorTestbed().Sut(), gnmi_config));

  // Sets up an example table entry.
  const sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
      R"pb(
        router_interface_table_entry {
          match { router_interface_id: "router-interface-1" }
          action {
            set_port_and_src_mac { port: "1" src_mac: "02:2a:10:00:00:03" }
          }
        }
      )pb");
  ASSERT_OK_AND_ASSIGN(p4::v1::TableEntry pi_entry,
                       pdpi::PdTableEntryToPi(GetIrP4Info(), pd_entry));

  ASSERT_OK(pdpi::InstallPiTableEntries(&GetSutP4RuntimeSession(),
                                        GetIrP4Info(), {pi_entry}));

  // Pushing the same Gnmi Config is also OK when entries are programmed.
  ASSERT_OK(pins_test::PushGnmiConfig(GetMirrorTestbed().Sut(), gnmi_config));
}

}  // namespace
}  // namespace pins_test

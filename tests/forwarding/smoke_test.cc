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
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "gutil/testing.h"
#include "p4_pdpi/entity_management.h"
#include "p4_pdpi/pd.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/test_data.h"

namespace gpins {
namespace {

// TODO: Enable once the bug is fixed.
TEST_P(SmokeTestFixture,
       DISABLED_InstallDefaultRouteForEmptyStringVrfShouldSucceed) {
  const sai::TableEntry pd_entry = gutil::ParseProtoOrDie<sai::TableEntry>(
      R"pb(
        ipv4_table_entry {
          match { vrf_id: "" }
          action { drop {} }
        }
      )pb");

  ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                       pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
  ASSERT_OK(pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry));
}

// TODO: Enable once the bug is fixed.
TEST_P(SmokeTestFixture, DISABLED_Bug181149419) {
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
                         pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
    EXPECT_OK(pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry));
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
                         pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
    EXPECT_FALSE(
        pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry).ok());
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
        ->mutable_mirror()
        ->set_mirror_session_id(absl::StrCat("session-", i));
    pd_entry.mutable_acl_ingress_table_entry()
        ->mutable_match()
        ->mutable_src_ip()
        ->set_value(absl::StrCat("10.0.0.", i));

    ASSERT_OK_AND_ASSIGN(const p4::v1::TableEntry pi_entry,
                         pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
    ASSERT_OK(pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry));
  }
}

TEST_P(SmokeTestFixture, InsertTableEntry) {
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
                       pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
  ASSERT_OK(pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry));
}

TEST_P(SmokeTestFixture, InsertTableEntryWithRandomCharacterId) {
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
                       pdpi::PdTableEntryToPi(IrP4Info(), pd_entry));
  ASSERT_OK(pdpi::InstallPiTableEntry(SutP4RuntimeSession(), pi_entry));
  ASSERT_OK_AND_ASSIGN(auto entries,
                       pdpi::ReadPiTableEntries(SutP4RuntimeSession()));
  ASSERT_EQ(entries.size(), 1);
  ASSERT_THAT(entries[0], gutil::EqualsProto(pi_entry));
}

TEST_P(SmokeTestFixture, InsertAndReadTableEntries) {
  pdpi::P4RuntimeSession* session = SutP4RuntimeSession();
  const pdpi::IrP4Info& ir_p4info = IrP4Info();
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

  ASSERT_OK(pdpi::InstallPiTableEntries(session, ir_p4info, write_pi_entries));

  p4::v1::ReadRequest read_request;
  read_request.add_entities()->mutable_table_entry();
  ASSERT_OK_AND_ASSIGN(p4::v1::ReadResponse read_response,
                       pdpi::SetIdAndSendPiReadRequest(session, read_request));

  for (const auto& entity : read_response.entities()) {
    ASSERT_OK(test_environment.AppendToTestArtifact(
        "pi_entries_read_back.pb.txt",
        absl::StrCat(entity.table_entry().DebugString(), "\n")));
  }

  // Compare the result in proto format since the fields being compared are
  // nested and out of order.
  EXPECT_THAT(read_response, gutil::EqualsProto(expected_read_response));
}

}  // namespace
}  // namespace gpins

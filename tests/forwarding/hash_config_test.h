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

#ifndef GOOGLE_TESTS_FORWARDING_HASH_CONFIG_TEST_H_
#define GOOGLE_TESTS_FORWARDING_HASH_CONFIG_TEST_H_

#include "absl/container/btree_map.h"
#include "absl/container/btree_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"
#include "p4/config/v1/p4info.pb.h"
#include "tests/forwarding/packet_test_util.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace pins_test {

// This class stores and reports data on received packets. Particularly, it
// keeps track of packets based on the egress port for the SUT / ingress port of
// the Control Switch.
// Test class for the hash config test.
class HashConfigTest : public thinkit::MirrorTestbedFixture {
 public:
  class TestData {
   public:
    // Map of port to the set of indices of received packets.
    // Sorted (btree) maps and sets help to make error messages more readable
    // when doing container comparisons.
    using ResultMap = absl::btree_map<std::string, absl::btree_set<int>>;

    // Return the results of the received packets.
    ResultMap Results() const ABSL_LOCKS_EXCLUDED(mutex_) {
      absl::MutexLock lock(&mutex_);
      return packets_by_port_;
    }

    // Add a received packet to this test data holder.
    void AddPacket(absl::string_view egress_port, packetlib::Packet packet)
        ABSL_LOCKS_EXCLUDED(mutex_);

    // Return the number of packets that have been received.
    int PacketCount() const ABSL_LOCKS_EXCLUDED(mutex_) {
      absl::MutexLock lock(&mutex_);
      return received_packets_.size();
    }

    // Log the packets while holding the mutex lock so we don't need to create
    // and return copy of received_packets_.
    absl::Status Log(thinkit::TestEnvironment& environment,
                     absl::string_view artifact_name)
        ABSL_LOCKS_EXCLUDED(mutex_);

   protected:
    // Mutex to guard the data values. Values are written by the receiver thread
    // and read by the main thread.
    mutable absl::Mutex mutex_;

    // Results as the set of packets arriving at each port.
    ResultMap packets_by_port_ ABSL_GUARDED_BY(mutex_);

    // In-order log of all the received packets paired with the egress port.
    // Useful for logging.
    std::vector<std::pair<std::string, packetlib::Packet>> received_packets_
        ABSL_GUARDED_BY(mutex_);
  };

  void SetUp() override;

  void TearDown() override;

  // Record the P4Info file for debugging.
  absl::Status RecordP4Info(absl::string_view test_stage,
                            const p4::config::v1::P4Info& p4info);

  // Reboot the SUT switch and wait for it to be ready.
  // The switch is considered ready when the test ports are up and the P4Runtime
  // session is reachable.
  void RebootSut();

  // Send and receive packets for a particular test config. Save the resulting
  // test data.
  void SendAndReceivePackets(const pdpi::IrP4Info& ir_p4info,
                             absl::string_view test_stage,
                             absl::string_view test_config_name,
                             const gpins::TestConfiguration& test_config,
                             TestData& test_data);

  // Send and receive packets for all test configs. Save the resulting test
  // data.
  void SendPacketsAndRecordResultsPerTestConfig(
      const p4::config::v1::P4Info p4info, absl::string_view test_stage,
      absl::node_hash_map<std::string, TestData>& output_record);

  // Run a test with the modified P4Info to compare the hash output with the
  // output from the original P4Info.
  void TestHashDifference(const p4::config::v1::P4Info& modified_p4info);

  void InitializeOriginalP4InfoTestDataIfNeeded();
  static const absl::node_hash_map<std::string, TestData>&
  OriginalP4InfoTestData() {
    return *original_p4info_test_data_;
  }

 protected:
  // Set of interfaces to hash against. There is a 1:1 mapping of interfaces_ to
  // port_ids_, but we don't care about the mapping in the test.
  std::vector<std::string> interfaces_;

  // Set of port IDs to hash against.
  absl::btree_set<int> port_ids_;

  // Test data from the original config.
  static absl::node_hash_map<std::string, TestData>* original_p4info_test_data_;
};

}  // namespace pins_test

#endif  // GOOGLE_TESTS_FORWARDING_HASH_CONFIG_TEST_H_

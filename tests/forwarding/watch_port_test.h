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

#ifndef GOOGLE_TESTS_FORWARDING_WATCH_PORT_TEST_H_
#define GOOGLE_TESTS_FORWARDING_WATCH_PORT_TEST_H_

#include <thread>  // NOLINT: Need threads (instead of fiber) for upstream code.
#include <vector>

#include "p4_pdpi/connection_management.h"
#include "tests/forwarding/group_programming_util.h"
#include "tests/forwarding/packet_test_util.h"
#include "thinkit/mirror_testbed_fixture.h"

namespace gpins {

// WatchPortTestFixture for testing watch port action.
class WatchPortTestFixture : public thinkit::MirrorTestbedFixture {
 protected:
  void SetUp() override;

  void TearDown() override;

  TestData test_data_;
  std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session_;
  std::unique_ptr<pdpi::P4RuntimeSession> control_p4_session_;
  std::unique_ptr<gnmi::gNMI::StubInterface> sut_gnmi_stub_;
  // Stores the receive thread that is created in SetUp() and joined in
  // TearDown(). Accesses control_p4_session_->StreamChannelRead to read
  // packets, which must not be used by other threads.
  std::thread receive_packet_thread_;
};

}  // namespace gpins

#endif  // GOOGLE_TESTS_FORWARDING_WATCH_PORT_TEST_H_

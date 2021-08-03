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
#ifndef GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_
#define GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_

#include <memory>

#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/fake_packetio_interface.h"
#include "swss/fakes/fake_sonic_db_table.h"
#include "swss/fakes/fake_system_state_helper.h"

namespace p4rt_app {
namespace test_lib {

struct P4RuntimeGrpcServiceOptions {
  bool use_genetlink = false;
  bool translate_port_ids = true;
};

class P4RuntimeGrpcService {
 public:
  explicit P4RuntimeGrpcService(const P4RuntimeGrpcServiceOptions& options);
  ~P4RuntimeGrpcService();

  int GrpcPort() const;

  // Accessors for AppDb tables.
  swss::FakeSonicDbTable& GetP4rtAppDbTable();
  swss::FakeSonicDbTable& GetPortAppDbTable();
  swss::FakeSonicDbTable& GetVrfAppDbTable();
  swss::FakeSonicDbTable& GetHashAppDbTable();
  swss::FakeSonicDbTable& GetSwitchAppDbTable();

  // Accessors for CounterDb tables.
  swss::FakeSonicDbTable& GetP4rtCountersDbTable();

  // Accessors for StateDb tables.
  swss::FakeSonicDbTable& GetP4rtStateDbTable();

  // Accessor for PacketIO interface.
  sonic::FakePacketIoInterface& GetFakePacketIoInterface();

  // Accessor for StateHelper interface.
  swss::FakeSystemStateHelper& GetSystemStateHelper();

 private:
  // Faked AppDb tables.
  swss::FakeSonicDbTable fake_p4rt_table_;
  swss::FakeSonicDbTable fake_vrf_table_;
  swss::FakeSonicDbTable fake_port_table_;
  swss::FakeSonicDbTable fake_hash_table_;
  swss::FakeSonicDbTable fake_switch_table_;

  // Faked CountersDb tables.
  swss::FakeSonicDbTable fake_p4rt_counters_table_;

  // Faked StateDb tables.
  swss::FakeSonicDbTable fake_p4rt_state_table_;

  std::unique_ptr<P4RuntimeImpl> p4runtime_server_;

  sonic::FakePacketIoInterface* fake_packetio_interface_;  // No ownership.

  // Faked system state helper.
  swss::FakeSystemStateHelper fake_system_state_helper_;

  std::unique_ptr<grpc::Server> server_;
};

}  // namespace test_lib
}  // namespace p4rt_app

#endif  // GOOGLE_P4RT_APP_TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_

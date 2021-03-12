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
#ifndef TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_
#define TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_

#include <memory>

#include "grpcpp/server.h"
#include "grpcpp/server_builder.h"
#include "p4rt_app/p4runtime/p4runtime_impl.h"
#include "p4rt_app/sonic/fake_packetio_interface.h"
#include "swss/fakes/fake_sonic_db_table.h"

namespace p4rt_app {
namespace test_lib {

class P4RuntimeGrpcService {
 public:
  P4RuntimeGrpcService();
  ~P4RuntimeGrpcService();

  int GrpcPort() const;
  swss::FakeSonicDbTable& GetP4rtAppDbTable();
  swss::FakeSonicDbTable& GetPortAppDbTable();
  swss::FakeSonicDbTable& GetVrfAppDbTable();
  sonic::FakePacketIoInterface& GetFakePacketIoInterface();

 private:
  swss::FakeSonicDbTable fake_p4rt_table_;
  swss::FakeSonicDbTable fake_vrf_table_;
  swss::FakeSonicDbTable fake_port_table_;
  std::unique_ptr<P4RuntimeImpl> p4runtime_server_;
  sonic::FakePacketIoInterface* fake_packetio_interface_;  // No ownership.

  std::unique_ptr<grpc::Server> server_;
};

}  // namespace test_lib
}  // namespace p4rt_app

#endif  // TESTS_LIB_P4RUNTIME_GRPC_SERVICE_H_

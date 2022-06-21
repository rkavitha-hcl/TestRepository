#ifndef GOOGLE_TESTS_SFLOW_SFLOW_TEST_H_
#define GOOGLE_TESTS_SFLOW_SFLOW_TEST_H_
#include <memory>
#include <string>
#include <thread>  // NOLINT: Need threads (instead of fiber) for upstream code.
#include <vector>

#include "absl/memory/memory.h"
#include "gtest/gtest.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "proto/gnmi/gnmi.grpc.pb.h"
#include "thinkit/generic_testbed_fixture.h"
#include "thinkit/ssh_client.h"

namespace gpins {

struct SflowTestParams {
  thinkit::GenericTestbedInterface* testbed_interface;
  thinkit::SSHClient* ssh_client;
  std::string gnmi_config;
  p4::config::v1::P4Info p4_info;
};

// Structure represents a link between SUT and Ixia.
// This is represented by Ixia interface name and the SUT's gNMI interface
// name and its corrosponding p4 runtime id.
struct IxiaLink {
  std::string ixia_interface;
  std::string sut_interface;
  int port_id;
};

class SflowTestFixture : public testing::TestWithParam<SflowTestParams> {
 protected:
  void SetUp() override;

  void TearDown() override;

  const p4::config::v1::P4Info& GetP4Info() { return GetParam().p4_info; }
  const pdpi::IrP4Info& GetIrP4Info() { return ir_p4_info_; }

  std::unique_ptr<thinkit::GenericTestbed> testbed_;
  pdpi::IrP4Info ir_p4_info_;
  std::unique_ptr<gnmi::gNMI::StubInterface> gnmi_stub_;
  std::unique_ptr<pdpi::P4RuntimeSession> sut_p4_session_;
  thinkit::SSHClient* ssh_client_ = GetParam().ssh_client;

  std::vector<IxiaLink> ready_links_;
};

}  // namespace gpins

#endif  // GOOGLE_TESTS_SFLOW_SFLOW_TEST_H_

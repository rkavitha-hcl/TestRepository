#include "p4_symbolic/sai/deparser.h"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "absl/status/statusor.h"
#include "glog/logging.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/packetlib/packetlib.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "p4_pdpi/string_encodings/bit_string.h"
#include "p4_symbolic/sai/fields.h"
#include "p4_symbolic/sai/parser.h"
#include "p4_symbolic/sai/sai.h"
#include "p4_symbolic/symbolic/symbolic.h"
#include "sai_p4/instantiations/google/switch_role.h"
#include "z3++.h"

namespace p4_symbolic {
namespace {

class SaiDeparserTest : public testing::TestWithParam<sai::SwitchRole> {
 public:
  void SetUp() override {
    testing::TestWithParam<sai::SwitchRole>::SetUp();
    sai::SwitchRole role = GetParam();
    std::vector<p4::v1::TableEntry> entries;
    std::vector<int> ports;
    ASSERT_OK_AND_ASSIGN(state_, EvaluateSaiPipeline(role, entries, ports));
  }

 protected:
  std::unique_ptr<symbolic::SolverState> state_;
};

TEST_P(SaiDeparserTest, DeparseIngressAndEgressHeadersWithoutConstraints) {
  ASSERT_EQ(state_->solver->check(), z3::check_result::sat);
  auto model = state_->solver->get_model();
  for (auto& packet :
       {state_->context.ingress_headers, state_->context.egress_headers}) {
    EXPECT_OK(SaiDeparser(packet, model).status());
  }
}

TEST_P(SaiDeparserTest, Ipv4PacketParserIntegrationTest) {
  // Add parse constraints.
  {
    ASSERT_OK_AND_ASSIGN(auto parse_constraints,
                         EvaluateSaiParser(state_->context.ingress_headers));
    for (auto& constraint : parse_constraints) state_->solver->add(constraint);
  }

  // Add IPv4 constraint.
  {
    ASSERT_OK_AND_ASSIGN(SaiFields fields,
                         GetSaiFields(state_->context.ingress_headers));
    state_->solver->add(fields.headers.ipv4.valid);
  }

  // Solve and deparse.
  ASSERT_EQ(state_->solver->check(), z3::check_result::sat);
  auto model = state_->solver->get_model();
  ASSERT_OK_AND_ASSIGN(std::string raw_packet,
                       SaiDeparser(state_->context.ingress_headers, model));

  // Check we indeed got an IPv4 packet.
  packetlib::Packet packet = packetlib::ParsePacket(raw_packet);
  LOG(INFO) << "Z3-generated packet = " << packet.DebugString();
  ASSERT_GE(packet.headers_size(), 2);
  ASSERT_TRUE(packet.headers(0).has_ethernet_header());
  ASSERT_TRUE(packet.headers(1).has_ipv4_header());
}

INSTANTIATE_TEST_SUITE_P(Instantiation, SaiDeparserTest,
                         testing::ValuesIn(sai::AllSwitchRoles()));

}  // namespace
}  // namespace p4_symbolic

// Copyright 2021 Google LLC
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
#include <memory>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "grpcpp/security/credentials.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "gutil/status_matchers.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4/v1/p4runtime.grpc.pb.h"
#include "p4/v1/p4runtime.pb.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/p4_runtime_session.h"
#include "p4rt_app/tests/lib/p4runtime_component_test_fixture.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "p4rt_app/tests/lib/p4runtime_request_helpers.h"
#include "sai_p4/instantiations/google/instantiations.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "swss/fakes/fake_sonic_db_table.h"

namespace p4rt_app {
namespace {

using ::gutil::StatusIs;
using ::testing::HasSubstr;

absl::StatusOr<std::unique_ptr<pdpi::P4RuntimeSession>> StartP4rtSession(
    const test_lib::P4RuntimeGrpcService& p4rt_service) {
  std::string address = absl::StrCat("localhost:", p4rt_service.GrpcPort());
  auto stub =
      pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());

  ASSIGN_OR_RETURN(auto p4rt_session,
                   pdpi::P4RuntimeSession::Create(std::move(stub),
                                                  /*device_id=*/183807201));
  return p4rt_session;
}

class PortNameAndIdTest : public testing::Test {
 protected:
  const p4::config::v1::P4Info p4_info_ =
      sai::GetP4Info(sai::Instantiation::kMiddleblock);
  const pdpi::IrP4Info ir_p4_info_ =
      sai::GetIrP4Info(sai::Instantiation::kMiddleblock);
};

TEST_F(PortNameAndIdTest, AddAThenDeletePortTranslation) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_OK(p4rt_service.AddPortTranslation("Ethernet0", "0"));
  EXPECT_OK(p4rt_service.RemovePortTranslation("Ethernet0"));
}

TEST_F(PortNameAndIdTest, AllowDuplicatePortTranslations) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_OK(p4rt_service.AddPortTranslation("Ethernet0", "0"));
  EXPECT_OK(p4rt_service.AddPortTranslation("Ethernet0", "0"));
}

TEST_F(PortNameAndIdTest, CannotReusePortTranslationsValues) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_OK(p4rt_service.AddPortTranslation("Ethernet0", "0"));

  // Cannot duplicate the port_name or the port_id.
  EXPECT_THAT(p4rt_service.AddPortTranslation("Ethernet0", "1"),
              StatusIs(absl::StatusCode::kAlreadyExists));
  EXPECT_THAT(p4rt_service.AddPortTranslation("Ethernet1", "0"),
              StatusIs(absl::StatusCode::kAlreadyExists));
}

TEST_F(PortNameAndIdTest, CannotAddPortTranslationWithEmptyValues) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_THAT(p4rt_service.AddPortTranslation("", "1"),
              StatusIs(absl::StatusCode::kInvalidArgument));
  EXPECT_THAT(p4rt_service.AddPortTranslation("Ethernet0", ""),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PortNameAndIdTest, RemovingNonExistantPortTranslationPasses) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_OK(p4rt_service.RemovePortTranslation("Ethernet0"));
}

TEST_F(PortNameAndIdTest, CannotRemovePortTranslationWithEmptyValues) {
  test_lib::P4RuntimeGrpcService p4rt_service =
      test_lib::P4RuntimeGrpcService(test_lib::P4RuntimeGrpcServiceOptions{});

  EXPECT_THAT(p4rt_service.RemovePortTranslation(""),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST_F(PortNameAndIdTest, ExpectingName) {
  // Start the P4RT server to accept port names, and configure a ethernet port
  // to NOT have an ID field.
  test_lib::P4RuntimeGrpcService p4rt_service = test_lib::P4RuntimeGrpcService(
      test_lib::P4RuntimeGrpcServiceOptions{.translate_port_ids = false});

  // Connect to the P4RT server and push a P4Info file.
  ASSERT_OK_AND_ASSIGN(auto p4rt_session, StartP4rtSession(p4rt_service));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      p4_info_));

  // Send a write request using the port name.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 router_interface_table_entry {
                                   match { router_interface_id: "16" }
                                   action {
                                     set_port_and_src_mac {
                                       port: "Ethernet0"
                                       src_mac: "00:02:03:04:05:06"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session.get(), request));
}

TEST_F(PortNameAndIdTest, ExpectingIdGetId) {
  // Start the P4RT server to accept port IDs, and configure a ethernet port
  // with an ID field.
  test_lib::P4RuntimeGrpcService p4rt_service = test_lib::P4RuntimeGrpcService(
      test_lib::P4RuntimeGrpcServiceOptions{.translate_port_ids = true});
  ASSERT_OK(p4rt_service.AddPortTranslation("Ethernet0", "1"));

  // Connect to the P4RT server and push a P4Info file.
  ASSERT_OK_AND_ASSIGN(auto p4rt_session, StartP4rtSession(p4rt_service));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      p4_info_));

  // Send a write request using the port ID.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 router_interface_table_entry {
                                   match { router_interface_id: "16" }
                                   action {
                                     set_port_and_src_mac {
                                       port: "1"
                                       src_mac: "00:02:03:04:05:06"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));
  EXPECT_OK(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session.get(), request));
}

TEST_F(PortNameAndIdTest, ExpectingIdGetName) {
  // Start the P4RT server to accept port IDs, and configure a ethernet port
  // with an ID field.
  test_lib::P4RuntimeGrpcService p4rt_service = test_lib::P4RuntimeGrpcService(
      test_lib::P4RuntimeGrpcServiceOptions{.translate_port_ids = true});
  ASSERT_OK(p4rt_service.AddPortTranslation("Ethernet0", "1"));

  // Connect to the P4RT server and push a P4Info file.
  ASSERT_OK_AND_ASSIGN(auto p4rt_session, StartP4rtSession(p4rt_service));
  ASSERT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session.get(),
      p4::v1::SetForwardingPipelineConfigRequest::RECONCILE_AND_COMMIT,
      p4_info_));

  // Send a write request using the port name.
  ASSERT_OK_AND_ASSIGN(p4::v1::WriteRequest request,
                       test_lib::PdWriteRequestToPi(
                           R"pb(
                             updates {
                               type: INSERT
                               table_entry {
                                 router_interface_table_entry {
                                   match { router_interface_id: "16" }
                                   action {
                                     set_port_and_src_mac {
                                       port: "Ethernet0"
                                       src_mac: "00:02:03:04:05:06"
                                     }
                                   }
                                 }
                               }
                             }
                           )pb",
                           ir_p4_info_));
  EXPECT_THAT(
      pdpi::SetMetadataAndSendPiWriteRequest(p4rt_session.get(), request),
      StatusIs(absl::StatusCode::kUnknown, HasSubstr("#1: INVALID_ARGUMENT")));
}

}  // namespace
}  // namespace p4rt_app

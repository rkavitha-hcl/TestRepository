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
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/entity_management.h"
#include "p4rt_app/tests/lib/p4runtime_grpc_service.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {

class ForwardingPipelineConfigTest : public testing::Test {
 protected:
  void SetUp() override {
    std::string address = absl::StrCat("127.0.0.1:", p4rt_service_.GrpcPort());
    LOG(INFO) << "Opening P4RT connection to " << address << ".";
    auto stub =
        pdpi::CreateP4RuntimeStub(address, grpc::InsecureChannelCredentials());
    ASSERT_OK_AND_ASSIGN(
        p4rt_session_, pdpi::P4RuntimeSession::Create(std::move(stub),
                                                      /*device_id=*/183807201));
  }

  test_lib::P4RuntimeGrpcService p4rt_service_;
  std::unique_ptr<pdpi::P4RuntimeSession> p4rt_session_;
};

TEST_F(ForwardingPipelineConfigTest, SetForwardingPipelineConfig) {
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(
      p4rt_session_.get(), sai::GetP4Info(sai::SwitchRole::kMiddleblock)));
}

TEST_F(ForwardingPipelineConfigTest, SetDuplicateForwardingPipelineConfig) {
  auto p4_info = sai::GetP4Info(sai::SwitchRole::kMiddleblock);
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(p4rt_session_.get(), p4_info));
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(p4rt_session_.get(), p4_info));
}

TEST_F(ForwardingPipelineConfigTest, ModifyConfig) {
  auto p4_info = sai::GetP4Info(sai::SwitchRole::kMiddleblock);
  EXPECT_OK(pdpi::SetForwardingPipelineConfig(p4rt_session_.get(), p4_info));
  p4_info.mutable_tables()->RemoveLast();
  EXPECT_THAT(pdpi::SetForwardingPipelineConfig(p4rt_session_.get(), p4_info),
              gutil::StatusIs(absl::StatusCode::kUnimplemented,
                              testing::HasSubstr("deleted: ")));
}

}  // namespace
}  // namespace p4rt_app

// Copyright 2022 Google LLC
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

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/proto_matchers.h"
#include "gutil/status_matchers.h"
#include "p4_pdpi/ir.h"
#include "p4rt_app/p4runtime/p4info_verification_schema.h"
#include "sai_p4/instantiations/google/sai_p4info.h"

namespace p4rt_app {
namespace {
TEST(P4InfoVerificationSchema, IsInSyncWithSaiP4Info) {
  ASSERT_OK_AND_ASSIGN(auto ir_p4info,
                       pdpi::CreateIrP4Info(sai::GetUnionedP4Info()));
  ASSERT_OK_AND_ASSIGN(auto supported_schema, SupportedSchema());
  ASSERT_THAT(ConvertToSchema(ir_p4info),
              gutil::IsOkAndHolds(gutil::EqualsProto(supported_schema)));
}
}  // namespace
}  // namespace p4rt_app

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
#include "p4_pdpi/testing/test_p4info.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "glog/logging.h"
#include "google/protobuf/text_format.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4_pdpi/ir.pb.h"
#include "p4_pdpi/testing/test_p4info_embed.h"

namespace pdpi {

using ::google::protobuf::TextFormat;
using ::gutil::FileToc;
using p4::config::v1::P4Info;
using pdpi::IrP4Info;

// Adapted from go/totw/128.
const P4Info& GetTestP4Info() {
  // Safe static object initialization following go/totw/110.
  static const P4Info* info = [] {
    const FileToc* const toc = test_p4info_embed_create();
    std::string data(toc[0].data, toc[0].size);
    P4Info* info = new P4Info();
    CHECK(  // Crash ok: TAP rules out failures.
        TextFormat::ParseFromString(data, info))
        << "unable to read embedded p4info text file";
    return info;
  }();
  return *info;
}

const IrP4Info& GetTestIrP4Info() {
  // Safe static object initialization following go/totw/110.
  static const IrP4Info* info = [] {
    absl::StatusOr<IrP4Info> ir_p4info = pdpi::CreateIrP4Info(GetTestP4Info());
    CHECK(ir_p4info.status().ok())  // Crash ok: TAP rules out failures.
        << ir_p4info.status();
    return new IrP4Info(std::move(ir_p4info).value());
  }();
  return *info;
}

}  // namespace pdpi

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

// This program produces a p4rt_app::P4InfoVerificationSchema from a provided
// P4Info.

#include <iostream>
#include <string>

#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "gflags/gflags.h"
#include "glog/logging.h"
#include "gutil/io.h"
#include "gutil/proto.h"
#include "p4/config/v1/p4info.pb.h"
#include "p4_pdpi/ir.h"
#include "p4rt_app/p4runtime/p4info_verification_schema.h"

DEFINE_string(p4info, "",
              "The source p4info file. If not provided, the p4info will be "
              "read from stdin.");
DEFINE_string(output, "",
              "The output file to store the schema. If not provided, the "
              "schema will be written to stdout.");

// Produces the P4Info from the --p4info flag or from stdin. Logs an error and
// returns false if the P4Info cannot be produces.
bool GetP4Info(p4::config::v1::P4Info& p4info) {
  const std::string& input_filename = FLAGS_p4info;
  if (input_filename.empty()) {
    std::string p4info_string;
    std::string line;
    while (std::getline(std::cin, line)) {
      absl::StrAppend(&p4info_string, line);
    }
    auto status = gutil::ReadProtoFromString(p4info_string, &p4info);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to parse input as p4::config::v1::P4Info: "
                 << status.message();
      return false;
    }
  } else {
    auto status = gutil::ReadProtoFromFile(input_filename, &p4info);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to read input file (" << input_filename
                 << ") as p4::config::v1::P4Info: " << status.message();
      return false;
    }
  }
  return true;
}

int main(int argc, char** argv) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Get the P4Info.
  p4::config::v1::P4Info p4info;
  if (!GetP4Info(p4info)) return 1;

  // Create the IrP4Info from the P4Info.
  auto ir_result = pdpi::CreateIrP4Info(p4info);
  if (!ir_result.ok()) {
    LOG(ERROR) << "Failed to translate P4Info to IrP4Info: "
               << ir_result.status().message();
    return 1;
  }

  // Create the P4InfoVerificationSchema from the IrP4Info.
  auto schema_result = p4rt_app::ConvertToSchema(*ir_result);
  if (!schema_result.ok()) {
    LOG(ERROR) << "Failed to produce schema: "
               << schema_result.status().message();
    return 1;
  }

  // Output the schema.
  std::string outfile = FLAGS_output;
  if (outfile.empty()) {
    std::cout << schema_result->DebugString();
  } else {
    auto status = gutil::WriteFile(schema_result->DebugString(), outfile);
    if (!status.ok()) {
      LOG(ERROR) << "Failed to write schema to file (" << outfile
                 << "): " << status.message();
      return 1;
    }
  }

  return 0;
}

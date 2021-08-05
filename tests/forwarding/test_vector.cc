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

#include "tests/forwarding/test_vector.h"

#include <ostream>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/types/optional.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/test_vector.pb.h"

namespace gpins {

using ::google::protobuf::util::MessageDifferencer;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::MatchResultListener;

bool PacketLessThan(const Packet* a, const Packet* b) {
  return a->hex() < b->hex();
}

bool PacketInLessThan(const PacketIn* a, const PacketIn* b) {
  return a->hex() < b->hex();
}

bool CompareSwitchOutputs(SwitchOutput actual_output,
                          SwitchOutput expected_output,
                          MatchResultListener* listener) {
  if (actual_output.packets_size() != expected_output.packets_size()) {
    *listener << "has wrong number of packets (actual: "
              << actual_output.packets_size()
              << " expected: " << expected_output.packets_size() << ")\n";
    return false;
  }

  if (actual_output.packet_ins_size() != expected_output.packet_ins_size()) {
    *listener << "has wrong number of packet ins (actual: "
              << actual_output.packet_ins_size()
              << " expected: " << expected_output.packet_ins_size() << ")\n";
    return false;
  }

  std::sort(actual_output.mutable_packets()->pointer_begin(),
            actual_output.mutable_packets()->pointer_end(), PacketLessThan);
  std::sort(expected_output.mutable_packets()->pointer_begin(),
            expected_output.mutable_packets()->pointer_end(), PacketLessThan);
  std::sort(actual_output.mutable_packet_ins()->pointer_begin(),
            actual_output.mutable_packet_ins()->pointer_end(),
            PacketInLessThan);
  std::sort(expected_output.mutable_packet_ins()->pointer_begin(),
            expected_output.mutable_packet_ins()->pointer_end(),
            PacketInLessThan);

  for (int i = 0; i < expected_output.packets_size(); ++i) {
    const Packet& actual_packet = actual_output.packets(i);
    const Packet& expected_packet = expected_output.packets(i);
    MessageDifferencer differ;
    std::string diff;
    differ.ReportDifferencesToString(&diff);
    if (!differ.Compare(expected_packet.parsed(), actual_packet.parsed())) {
      *listener << "has packet " << i << " with wrong header fields:\n" << diff;
      return false;
    }
  }

  for (int i = 0; i < expected_output.packet_ins_size(); ++i) {
    const PacketIn& actual_packet_in = actual_output.packet_ins(i);
    const PacketIn& expected_packet_in = expected_output.packet_ins(i);

    MessageDifferencer differ;
    std::string diff;
    differ.ReportDifferencesToString(&diff);
    if (!differ.Compare(expected_packet_in.parsed(),
                        actual_packet_in.parsed())) {
      *listener << "has packet in " << i << " with wrong header fields:\n"
                << diff;
      return false;
    }
    if (!differ.Compare(expected_packet_in.metadata(),
                        actual_packet_in.metadata())) {
      *listener << "has packet in " << i << " with wrong metadata fields:\n"
                << diff;
      return false;
    }
  }

  *listener << "matches\n";
  return true;
}

class ConformsToTestVectorMatcher
    : public MatcherInterface<const SwitchOutput&> {
 public:
  explicit ConformsToTestVectorMatcher(TestVector test_vector)
      : test_vector_(std::move(test_vector)) {}

  void DescribeTo(::std::ostream* os) const override {
    *os << test_vector_.DebugString();
  }

  bool MatchAndExplain(const SwitchOutput& actual_output,
                       MatchResultListener* listener) const override {
    bool match = false;
    *listener << "which compares to the acceptable outputs as follows:\n";
    for (int i = 0; i < test_vector_.acceptable_outputs_size(); ++i) {
      const SwitchOutput& expected_output = test_vector_.acceptable_outputs(i);
      *listener << "- alternative " << i << " ";
      if (CompareSwitchOutputs(actual_output, expected_output, listener)) {
        match = true;
      }
    }
    return match;
  }

 private:
  const TestVector test_vector_;
};

Matcher<const SwitchOutput&> ConformsToTestVector(TestVector test_vector) {
  return MakeMatcher(new ConformsToTestVectorMatcher(std::move(test_vector)));
}

absl::optional<std::string> CheckForTestVectorFailure(
    const TestVector& test_vector, const SwitchOutput& actual_output) {
  // TODO: Get rid of matchers and implement logic here instead.
  // Using gunit internals here is not quite kosher.
  auto matcher =
      Matcher<SwitchOutput>(new ConformsToTestVectorMatcher(test_vector));
  testing::AssertionResult result =
      testing::internal::MakePredicateFormatterFromMatcher(matcher)(
          "actual gpins::SwitchOutput", actual_output);
  if (result) {
    return absl::nullopt;
  } else {
    return result.message();
  }
}

}  // namespace gpins

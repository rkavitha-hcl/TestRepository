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
#include <tuple>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "gutil/status.h"
#include "p4_pdpi/packetlib/packetlib.pb.h"
#include "sai_p4/instantiations/google/sai_pd.pb.h"
#include "tests/forwarding/test_vector.pb.h"

namespace gpins {
namespace {

using ::google::protobuf::util::MessageDifferencer;
using ::testing::MatchResultListener;

// -- Detailed comparison of actual vs expected `SwitchOutput`s ----------------

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
    *listener << "has mismatched number of packets (actual: "
              << actual_output.packets_size()
              << " expected: " << expected_output.packets_size() << ")\n";
    return false;
  }

  if (actual_output.packet_ins_size() != expected_output.packet_ins_size()) {
    *listener << "has mismatched number of packet ins (actual: "
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
      *listener << "has packet " << i << " with mismatched header fields:\n  "
                << diff;
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
      *listener << "has packet in " << i
                << " with mismatched header fields:\n  " << diff;
      return false;
    }
    if (!differ.Compare(expected_packet_in.metadata(),
                        actual_packet_in.metadata())) {
      *listener << "has packet in " << i
                << " with different metadata fields:\n  " << diff;
      return false;
    }
  }

  *listener << "matches\n";
  return true;
}

// Compares the `actual_output` to the `acceptable_outputs` in the given
// `test_vector`, returning `absl::nullopt` if the actual output is acceptable,
// or an explanation of why it is not otherwise.
absl::optional<std::string> CompareSwitchOutputs(
    const TestVector test_vector, const SwitchOutput& actual_output) {
  testing::StringMatchResultListener listener;
  for (int i = 0; i < test_vector.acceptable_outputs_size(); ++i) {
    const SwitchOutput& expected_output = test_vector.acceptable_outputs(i);
    listener << "- acceptable output #" << (i + 1) << " ";
    if (CompareSwitchOutputs(actual_output, expected_output, &listener)) {
      return absl::nullopt;
    }
  }
  return listener.str();
}

// -- Simplified switch output characterizations -------------------------------

// Characterization of a `SwitchOutput` in terms of two key metrics: how many
// packets got forwarded and how many got punted?
// The purpose of this struct is to give a compact summary of a switch output
// that is easy to understand. This is useful in error messages, because actual
// `SwitchOutput`s are complex (they specify packet header fields and payloads,
// for example) and so dumping them directly is overwhelming.
struct SwitchOutputCharacterization {
  int num_forwarded;
  int num_punted;

  // https://abseil.io/docs/cpp/guides/hash#tldr-how-do-i-make-my-type-hashable
  template <typename H>
  friend H AbslHashValue(H h, const SwitchOutputCharacterization& x) {
    return H::combine(std::move(h), x.num_forwarded, x.num_punted);
  }
};

bool operator==(const SwitchOutputCharacterization& x,
                const SwitchOutputCharacterization& y) {
  return x.num_forwarded == y.num_forwarded && x.num_punted == y.num_punted;
}

// Returns a simple characterization of the given `output`.
SwitchOutputCharacterization CharacterizeSwitchOutput(
    const SwitchOutput& output) {
  return SwitchOutputCharacterization{
      .num_forwarded = output.packets_size(),
      .num_punted = output.packet_ins_size(),
  };
}

// Returns a human-readable description of the given `output`, for use in error
// messages.
std::string DescribeSwitchOutput(const SwitchOutputCharacterization& output) {
  const bool forwarded = output.num_forwarded > 0;
  const bool punted = output.num_punted > 0;
  if (forwarded && punted)
    return absl::Substitute("forwarded ($0 copies) and punted ($1 copies)",
                            output.num_forwarded, output.num_punted);
  if (forwarded && !punted)
    return absl::Substitute("forwarded ($0 copies)", output.num_forwarded);
  if (!forwarded && punted)
    return absl::Substitute("punted ($0 copies)", output.num_punted);
  return "dropped";
}

// Returns a human-readable description of the expectation encoded by the given
// `acceptable_output_characterizations`, for use in error messages.
std::string DescribeExpectation(
    const absl::flat_hash_set<SwitchOutputCharacterization>&
        acceptable_output_characterizations) {
  // This should never happen, but it is convenient for this function to be pure
  // and so we handle the case gracefully and without erroring.
  if (acceptable_output_characterizations.empty())
    return "false (will always fail)";
  // In practice, while there are often multiple acceptable outputs
  // (e.g., due to WCMP), all of them tend to have the same *output
  // characterization*. So this function is optimized for the case
  // `acceptable_output_characterizations.size() == 1` and doesn't try to be
  // clever otherwise.
  return absl::StrJoin(acceptable_output_characterizations, ", or ",
                       [](std::string* output, auto& acceptable_output) {
                         absl::StrAppend(
                             output, "packet gets ",
                             DescribeSwitchOutput(acceptable_output));
                       });
}

// Returns a human-readable description of the given `actual_output`, for use in
// error messages.
std::string DescribeActual(const SwitchOutputCharacterization& actual_output) {
  return absl::StrCat("packet got ", DescribeSwitchOutput(actual_output));
}

// Returns whether the packet with the given `characterization` got dropped.
bool IsCharacterizedAsDrop(
    const SwitchOutputCharacterization& characterization) {
  return characterization.num_forwarded == 0 &&
         characterization.num_punted == 0;
}

// Returns whether the packet with the given possible `characterizations`
// surely (according to all characterizations) got dropped.
bool IsCharacterizedAsDrop(
    const absl::flat_hash_set<SwitchOutputCharacterization>&
        characterizations) {
  return characterizations.size() == 1 &&
         IsCharacterizedAsDrop(*characterizations.cbegin());
}

static constexpr absl::string_view kActualBanner =
    "== ACTUAL "
    "======================================================================";
static constexpr absl::string_view kExpectationBanner =
    "== EXPECTATION "
    "=================================================================";
static constexpr absl::string_view kInputBanner =
    "== INPUT "
    "=======================================================================";

}  // namespace

absl::optional<std::string> CheckForTestVectorFailure(
    const TestVector& test_vector, const SwitchOutput& actual_output) {
  const absl::optional<std::string> diff =
      CompareSwitchOutputs(test_vector, actual_output);
  if (!diff.has_value()) return absl::nullopt;

  // To make the diff more digestible, we first give an abstract
  // characterization of the expected and actual outputs.
  absl::flat_hash_set<SwitchOutputCharacterization>
      acceptable_characterizations;
  for (auto& acceptable_output : test_vector.acceptable_outputs()) {
    acceptable_characterizations.insert(
        CharacterizeSwitchOutput(acceptable_output));
  }
  const SwitchOutputCharacterization actual_characterization =
      CharacterizeSwitchOutput(actual_output);
  const bool actual_characterization_matches_expected_one =
      acceptable_characterizations.contains(actual_characterization);

  std::string expectation = DescribeExpectation(acceptable_characterizations);
  std::string actual = DescribeActual(actual_characterization);
  if (actual_characterization_matches_expected_one) {
    absl::StrAppend(&actual, ", but with unexpected modifications");
  }
  std::string failure = absl::Substitute(
      "Expected: $0\n  Actual: $1\n$2\nDetails dumped below.\n\n", expectation,
      actual, *diff);

  // Dump actual output, if any.
  if (!IsCharacterizedAsDrop(actual_characterization)) {
    absl::StrAppend(&failure, kActualBanner, "\n", actual_output.DebugString());
  }

  // Dump expected output, if any.
  if (!IsCharacterizedAsDrop(acceptable_characterizations)) {
    absl::StrAppend(&failure, kExpectationBanner, "\n");
    for (int i = 0; i < test_vector.acceptable_outputs_size(); ++i) {
      absl::StrAppendFormat(
          &failure, "-- Acceptable output: Alternative #%d --\n%s", (i + 1),
          test_vector.acceptable_outputs(i).DebugString());
    }
  }

  // Dump input.
  absl::StrAppend(&failure, kInputBanner, "\n",
                  test_vector.input().DebugString());

  return failure;
}

}  // namespace gpins

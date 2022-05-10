#ifndef GOOGLE_TESTS_FORWARDING_MATCH_ACTION_COVERAGE_TEST_H_
#define GOOGLE_TESTS_FORWARDING_MATCH_ACTION_COVERAGE_TEST_H_

#include "tests/forwarding/mirror_blackbox_test_fixture.h"

namespace p4_fuzzer {

// This test installs at least one table entry using (or not using if possible)
// each match field and each action in each possible table.
class MatchActionCoverageTestFixture
    : public pins_test::MirrorBlackboxTestFixture {};

}  // namespace p4_fuzzer

#endif  // GOOGLE_TESTS_FORWARDING_MATCH_ACTION_COVERAGE_TEST_H_

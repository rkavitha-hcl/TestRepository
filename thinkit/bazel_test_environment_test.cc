#include "thinkit/bazel_test_environment.h"

#include <cstdlib>

// Switching benchmark dependency to third_party seems to not output any
// benchmarking information when run.
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"

namespace thinkit {
namespace {

using ::gutil::IsOk;

// -- Tests --------------------------------------------------------------------

constexpr absl::string_view kTestArtifact = "my_test_artifact.txt";

class BazelTestEnvironmentTest : public testing::Test {
 protected:
  std::unique_ptr<TestEnvironment> environment_ =
      absl::make_unique<BazelTestEnvironment>(/*mask_known_failures=*/true);
};

TEST_F(BazelTestEnvironmentTest, StoreTestArtifact) {
  EXPECT_OK(environment_->StoreTestArtifact(kTestArtifact, "Hello, World!\n"));
  EXPECT_OK(environment_->StoreTestArtifact(kTestArtifact, "Hello, Test!\n"));
}

TEST_F(BazelTestEnvironmentTest, AppendToTestArtifact) {
  EXPECT_OK(
      environment_->AppendToTestArtifact(kTestArtifact, "Hello, World!\n"));
  EXPECT_OK(
      environment_->AppendToTestArtifact(kTestArtifact, "Hello, Test!\n"));
}

// -- Benchmarks ---------------------------------------------------------------
//
// Best run with 'blaze test --benchmarks=all'.
//
// Ideally, we would like to use 'benchy' for benchmarking purposes, but,
// because we are benchmarking a testing environment, it relies on being set
// up as a test environment.
//
// For now, one can manually set the environment variable TEST_TMPDIR, then run
// it with benchy, but do not expect that to remain feasible since we may rely
// on additional parts of the test environment in the future.

// The state specifies the size of the written string in Bytes.
void RunBenchmark(benchmark::State& state, bool truncate) {
  const int size = state.range(0);
  const std::string filename = "benchmark_file";
  BazelTestEnvironment env(false);

  std::string str(size, 'a');
  if (truncate) {
    for (auto s : state) {
      ASSERT_THAT(env.StoreTestArtifact(filename, str), IsOk());
    }
  } else {
    for (auto s : state) {
      ASSERT_THAT(env.AppendToTestArtifact(filename, str), IsOk());
    }
  }
  // Outputs number of iterations (items) per second.
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}

void BM_Bazel_AppendToTestArtifact(benchmark::State& state) {
  RunBenchmark(state, /*truncate=*/false);
}

BENCHMARK(BM_Bazel_AppendToTestArtifact)
    ->Args({/*write_size in bytes=*/1})
    ->Args({1024})
    ->Args({1024 * 1024});

void BM_Bazel_StoreTestArtifact(benchmark::State& state) {
  RunBenchmark(state, /*truncate=*/true);
}

BENCHMARK(BM_Bazel_StoreTestArtifact)
    ->Args({/*write_size in bytes=*/1})
    ->Args({1024})
    ->Args({1024 * 1024});
}  // namespace
}  // namespace thinkit

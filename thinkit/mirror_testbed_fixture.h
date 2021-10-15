// Copyright (c) 2021, Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef GOOGLE_THINKIT_MIRROR_TESTBED_TEST_FIXTURE_H_
#define GOOGLE_THINKIT_MIRROR_TESTBED_TEST_FIXTURE_H_

#include <memory>

#include "absl/memory/memory.h"
#include "gtest/gtest.h"
#include "p4_pdpi/ir.pb.h"
#include "sai_p4/instantiations/google/sai_p4info.h"
#include "thinkit/mirror_testbed.h"

namespace thinkit {

// The ThinKit `MirrorTestbedInterface` defines an interface every test platform
// should implement. The expectations are such that the MirrorTestbed should
// only be accessed after SetUp() is called and before TearDown() is called.
class MirrorTestbedInterface {
 public:
  virtual ~MirrorTestbedInterface() = default;

  virtual void SetUp() = 0;
  virtual void TearDown() = 0;

  virtual MirrorTestbed& GetMirrorTestbed() = 0;
};

// The Thinkit `MirrorTestbedFixtureParams` defines test parameters to
// `MirrorTestbedFixture` class.
struct MirrorTestbedFixtureParams {
  // Ownership transferred in MirrorTestbedFixture class.
  MirrorTestbedInterface* mirror_testbed;
  std::string gnmi_config;
  absl::optional<std::vector<int>> port_ids;
};

// The ThinKit `MirrorTestbedFixture` class acts as a base test fixture for
// platform independent PINS tests. Any platform specific SetUp or TearDown
// requirements are abstracted through the ThinKit MirrorTestbedInterface which
// is passed as a parameter.
//
// New PINS tests should extend this fixture, and if needed can extend the
// SetUp() and/or TearDown() methods:
//    class MyPinsTest : public thinkit::MirrorTestbedFixture {
//      void SetUp() override {
//        MirrorTestbedFixture::SetUp();  // called first.
//
//        // custom setup steps ...
//      }
//
//      void TearDown() override {
//        // custom tear down steps ...
//
//        MirrorTestbedFixture::TearDown();  // called last.
//      }
//    };
//
//  Individual tests should use the new suite name:
//    TEST_P(MyPinsTest, MyTestName) {}
class MirrorTestbedFixture
    : public testing::TestWithParam<MirrorTestbedFixtureParams> {
 protected:
  // A derived class that needs/wants to do its own setup can override this
  // method. However, it should take care to call this base setup first. That
  // will ensure the platform is ready, and in a healthy state.
  void SetUp() override { mirror_testbed_interface_->SetUp(); }

  // A derived class that needs/wants to do its own teardown can override this
  // method. However, it should take care to call this base teardown last. Once
  // this method is called accessing the platform can result in unexpected
  // behaviors.
  void TearDown() override { mirror_testbed_interface_->TearDown(); }

  // Accessor for the mirror testbed. This is only safe to be called after the
  // SetUp has completed.
  MirrorTestbed& GetMirrorTestbed() {
    return mirror_testbed_interface_->GetMirrorTestbed();
  }

  std::string GetGnmiConfig() { return GetParam().gnmi_config; }

  // Get the list of optional port ids.
  absl::optional<std::vector<int>> GetPortIds() { return GetParam().port_ids; }

  // TODO: Parameterize over the different instantiations like
  // MiddleBlock, FBR400.
  const p4::config::v1::P4Info& GetP4Info() {
    return sai::GetP4Info(sai::Instantiation::kMiddleblock);
  }
  const pdpi::IrP4Info& GetIrP4Info() {
    return sai::GetIrP4Info(sai::Instantiation::kMiddleblock);
  }

 private:
  // Takes ownership of the MirrorTestbedInterface parameter.
  std::unique_ptr<MirrorTestbedInterface> mirror_testbed_interface_ =
      absl::WrapUnique<MirrorTestbedInterface>(GetParam().mirror_testbed);
};

}  // namespace thinkit

#endif  // GOOGLE_THINKIT_MIRROR_TESTBED_TEST_FIXTURE_H_

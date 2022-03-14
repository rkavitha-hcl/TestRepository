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
#include "p4rt_app/sonic/packetio_impl.h"

#include <linux/if.h>
#include <unistd.h>

#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/memory/memory.h"
#include "absl/status/status.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"
#include "p4rt_app/sonic/adapters/mock_system_call_adapter.h"

namespace p4rt_app {
namespace sonic {
namespace {

using ::gutil::StatusIs;
using ::testing::Eq;
using ::testing::Return;

absl::Status EmptyPacketInCallback(std::string source_port,
                                   std::string tartget_port,
                                   std::string payload) {
  return absl::OkStatus();
}

TEST(PacketIoImplTest, SuccessOnAddPacketIoPort) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd[2];
  absl::Cleanup cleanup([&] {
    close(fd[0]);
    close(fd[1]);
  });
  EXPECT_GE(pipe(fd), 0);

  // Expect socket and if_nametoindex calls for the 2 ports getting added.
  EXPECT_CALL(*mock_call_adapter, socket)
      .Times(2)
      .WillRepeatedly(Return(fd[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex)
      .Times(2)
      .WillRepeatedly(Return(1));

  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.AddPacketIoPort(kSubmitToIngress));

  // Check that ports are valid for transmit and receive.
  EXPECT_TRUE(packetio_impl.IsValidPortForTransmit("Ethernet0"));
  EXPECT_TRUE(packetio_impl.IsValidPortForTransmit(kSubmitToIngress));
  EXPECT_TRUE(packetio_impl.IsValidPortForReceive("Ethernet0"));
  EXPECT_TRUE(packetio_impl.IsValidPortForReceive(kSubmitToIngress));
}

TEST(PacketIoImplTest, NoOpOnAddingDuplicatePacketIoPorts) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd[2];
  absl::Cleanup cleanup([&] {
    close(fd[0]);
    close(fd[1]);
  });
  EXPECT_GE(pipe(fd), 0);

  // Expect only one socket and if_nametoindex call.
  EXPECT_CALL(*mock_call_adapter, socket).WillOnce(Return(fd[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex).WillOnce(Return(1));

  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  EXPECT_TRUE(packetio_impl.IsValidPortForTransmit("Ethernet0"));
  EXPECT_TRUE(packetio_impl.IsValidPortForReceive("Ethernet0"));
}

TEST(PacketIoImplTest, NoActionOnAddingNonSdnPacketIoPorts) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  EXPECT_CALL(*mock_call_adapter, socket).Times(0);
  EXPECT_CALL(*mock_call_adapter, if_nametoindex).Times(0);
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("loopback0"));

  // Checks that ports are not valid for transmit and receive.
  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit("loopback0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForReceive("loopback0"));
}

TEST(PacketIoImplTest, SuccessOnRemovePacketIoPort) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd0[2], fd1[2];
  absl::Cleanup cleanup([&] {
    // Close the receive fd's only as transmit fd's are closed in the
    // RemovePacketIoPort.
    close(fd0[0]);
    close(fd1[0]);
  });

  EXPECT_GE(pipe(fd0), 0);
  EXPECT_GE(pipe(fd1), 0);

  EXPECT_CALL(*mock_call_adapter, socket)
      .WillOnce(Return(fd0[1]))
      .WillOnce(Return(fd1[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex)
      .Times(2)
      .WillRepeatedly(Return(1));

  // Expect close calls on the 2 socket fd's.
  EXPECT_CALL(*mock_call_adapter, close(Eq(fd0[1]))).Times(1);
  EXPECT_CALL(*mock_call_adapter, close(Eq(fd1[1]))).Times(1);

  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.AddPacketIoPort(kSubmitToIngress));
  ASSERT_OK(packetio_impl.RemovePacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.RemovePacketIoPort(kSubmitToIngress));

  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit("Ethernet0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit(kSubmitToIngress));
  EXPECT_FALSE(packetio_impl.IsValidPortForReceive("Ethernet0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForReceive(kSubmitToIngress));
}

TEST(PacketIoImplTest, FailOnNonExistentRemovePacketIoPort) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  EXPECT_CALL(*mock_call_adapter, close).Times(0);
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  EXPECT_THAT(packetio_impl.RemovePacketIoPort("Ethernet0"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PacketIoImplTest, FailOnRemoveDuplicatePacketIoPort) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd[2];
  absl::Cleanup cleanup([&] { close(fd[0]); });
  EXPECT_GE(pipe(fd), 0);
  EXPECT_CALL(*mock_call_adapter, socket).WillOnce(Return(fd[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex).WillOnce(Return(1));
  EXPECT_CALL(*mock_call_adapter, close(Eq(fd[1]))).Times(1);
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.RemovePacketIoPort("Ethernet0"));
  EXPECT_THAT(packetio_impl.RemovePacketIoPort("Ethernet0"),
              StatusIs(absl::StatusCode::kInvalidArgument));
}

TEST(PacketIoImplTest, NoActionOnRemovingNonSdnPacketIoPort) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  EXPECT_CALL(*mock_call_adapter, close).Times(0);
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                             });
  ASSERT_OK(packetio_impl.RemovePacketIoPort("loopback0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForReceive("loopback0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit("loopback0"));
}

TEST(PacketIoImplTest, SuccessOnAddPacketIoPortWithGenetlink) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd[2];
  absl::Cleanup cleanup([&] {
    close(fd[0]);
    close(fd[1]);
  });
  EXPECT_GE(pipe(fd), 0);
  EXPECT_CALL(*mock_call_adapter, socket)
      .Times(2)
      .WillRepeatedly(Return(fd[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex)
      .Times(2)
      .WillRepeatedly(Return(1));
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                                 .use_genetlink = true,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.AddPacketIoPort(kSubmitToIngress));
  EXPECT_TRUE(packetio_impl.IsValidPortForTransmit("Ethernet0"));
  EXPECT_TRUE(packetio_impl.IsValidPortForTransmit(kSubmitToIngress));
}

TEST(PacketIoImplTest, SuccessOnRemovePacketIoPortWithGenetlink) {
  auto mock_call_adapter = absl::make_unique<sonic::MockSystemCallAdapter>();
  int fd[2];
  absl::Cleanup cleanup([&] { close(fd[0]); });
  EXPECT_GE(pipe(fd), 0);
  EXPECT_CALL(*mock_call_adapter, socket)
      .Times(2)
      .WillRepeatedly(Return(fd[1]));
  EXPECT_CALL(*mock_call_adapter, if_nametoindex)
      .Times(2)
      .WillRepeatedly(Return(1));
  EXPECT_CALL(*mock_call_adapter, close).Times(2);
  PacketIoImpl packetio_impl(std::move(mock_call_adapter),
                             PacketIoOptions{
                                 .callback_function = EmptyPacketInCallback,
                                 .use_genetlink = true,
                             });
  ASSERT_OK(packetio_impl.AddPacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.AddPacketIoPort(kSubmitToIngress));
  ASSERT_OK(packetio_impl.RemovePacketIoPort("Ethernet0"));
  ASSERT_OK(packetio_impl.RemovePacketIoPort(kSubmitToIngress));
  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit("Ethernet0"));
  EXPECT_FALSE(packetio_impl.IsValidPortForTransmit(kSubmitToIngress));
}

}  // namespace
}  // namespace sonic
}  // namespace p4rt_app

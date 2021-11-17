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

#include "p4rt_app/authz_policy/authz_policy_checker.h"

#include <memory>

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"
#include "gutil/status_matchers.h"

namespace p4rt_app {
namespace grpc_authz_processor {
namespace {

TEST(AuthzPolicyCheckerTest, AllowsKnownUsersInConfiguredRpc) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value {
            rpc_policies {
              key: "rpc1"
              value { labels: "label1" labels: "label2" }
            }
          }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_OK(checker.Check("service1", "rpc1", "user1"));
  EXPECT_OK(checker.Check("service1", "rpc1", "user2"));
  EXPECT_OK(checker.Check("service1", "rpc1", "user3"));
}

TEST(AuthzPolicyCheckerTest, DeniesUnknownUsersInConfiguredRpc) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value {
            rpc_policies {
              key: "rpc1"
              value { labels: "label1" labels: "label2" }
            }
          }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_THAT(checker.Check("service1", "rpc1", "user4"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST(AuthzPolicyCheckerTest, AllowsKnownUsersInDefaultRpc) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value { default_service_policy { labels: "label1" labels: "label2" } }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_OK(checker.Check("service1", "rpc1", "user1"));
  EXPECT_OK(checker.Check("service1", "rpc1", "user2"));
  EXPECT_OK(checker.Check("service1", "rpc1", "user3"));
}

TEST(AuthzPolicyCheckerTest, DeniesUnknownUsersInDefaultRpc) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value { default_service_policy { labels: "label1" labels: "label2" } }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_THAT(checker.Check("service1", "rpc1", "user4"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST(AuthzPolicyCheckerTest, DeniesUsersInUnconfiguredRpcWithNoDefault) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value {
            rpc_policies {
              key: "rpc1"
              value { labels: "label1" labels: "label2" }
            }
          }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_THAT(checker.Check("service1", "rpc2", "user1"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service1", "rpc2", "user2"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service1", "rpc2", "user3"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service1", "rpc2", "user4"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
}

TEST(AuthzPolicyCheckerTest, DeniesUsersInUnconfiguredService) {
  google::p4rt::AuthorizationPolicy policy;
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        labels_to_principals {
          key: "label1"
          value { mdb_users: "user1" mdb_users: "user2" }
        }
        labels_to_principals {
          key: "label2"
          value { mdb_users: "user2" mdb_users: "user3" }
        }
        service_policies {
          key: "service1"
          value {
            rpc_policies {
              key: "rpc1"
              value { labels: "label1" labels: "label2" }
            }
          }
        }
      )pb",
      &policy));

  GrpcAuthzPolicyChecker checker(policy);
  EXPECT_THAT(checker.Check("service2", "rpc1", "user1"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service2", "rpc1", "user2"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service2", "rpc1", "user3"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
  EXPECT_THAT(checker.Check("service2", "rpc1", "user4"),
              gutil::StatusIs(absl::StatusCode::kPermissionDenied));
}

}  // namespace
}  // namespace grpc_authz_processor
}  // namespace p4rt_app

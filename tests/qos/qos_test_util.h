#ifndef GOOGLE_TESTS_QOS_QOS_TEST_UTIL_H_
#define GOOGLE_TESTS_QOS_QOS_TEST_UTIL_H_

#include <cstdint>
#include <optional>
#include <string>

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"
#include "lib/gnmi/openconfig.pb.h"
#include "proto/gnmi/gnmi.grpc.pb.h"
#include "proto/gnmi/gnmi.pb.h"
#include "thinkit/generic_testbed.h"
#include "thinkit/proto/generic_testbed.pb.h"

namespace pins_test {

// The maximum time the switch is allowed to take before queue counters read via
// gNMI have to be incremented after a packet hits a queue.
// Empirically, for PINS, queue counters currently seem to get updated every
// 10 seconds.
constexpr absl::Duration kMaxQueueCounterUpdateTime = absl::Seconds(25);

// These are the counters we track in these tests.
struct QueueCounters {
  int64_t num_packets_transmitted = 0;
  int64_t num_packets_dropped = 0;
};

// Operator to pretty print Queue Counters.
inline std::ostream &operator<<(std::ostream &os,
                                const QueueCounters &counters) {
  return os << absl::StreamFormat(
             "QueueCounters{"
             ".num_packets_transmitted = %d, "
             ".num_packets_dropped = %d"
             "}",
             counters.num_packets_transmitted, counters.num_packets_dropped);
}

QueueCounters operator-(const QueueCounters &x, const QueueCounters &y);

// Get queue counters for a port queue.
absl::StatusOr<QueueCounters> GetGnmiQueueCounters(
    absl::string_view port, absl::string_view queue,
    gnmi::gNMI::StubInterface &gnmi_stub);

// Get total packets (transmitted + dropped) for port queue.
int64_t TotalPacketsForQueue(const QueueCounters &counters);

// Parse IPv4 DSCP to queue mapping from gnmi configuration.
absl::StatusOr<absl::flat_hash_map<int, std::string>>
ParseIpv4DscpToQueueMapping(absl::string_view gnmi_config);

// Parse IPv6 DSCP to queue mapping from gnmi configuration.
absl::StatusOr<absl::flat_hash_map<int, std::string>>
ParseIpv6DscpToQueueMapping(absl::string_view gnmi_config);

// Get IPv4 DSCP to queue mapping from switch.
absl::StatusOr<absl::flat_hash_map<int, std::string>> GetIpv4DscpToQueueMapping(
    absl::string_view port, gnmi::gNMI::StubInterface &gnmi_stub);

// Get IPv6 DSCP to queue mapping from switch.
absl::StatusOr<absl::flat_hash_map<int, std::string>> GetIpv6DscpToQueueMapping(
    absl::string_view port, gnmi::gNMI::StubInterface &gnmi_stub);

// Get queue to IPv4 DSCP mapping from switch.
absl::StatusOr<absl::flat_hash_map<std::string, std::vector<int>>>
GetQueueToIpv4DscpsMapping(absl::string_view port,
                           gnmi::gNMI::StubInterface &gnmi_stub);

// Get queue to IPv6 DSCP mapping from switch.
absl::StatusOr<absl::flat_hash_map<std::string, std::vector<int>>>
GetQueueToIpv6DscpsMapping(absl::string_view port,
                           gnmi::gNMI::StubInterface &gnmi_stub);

// Get name of queue configured for the given DSCP.
absl::StatusOr<std::string> GetQueueNameByDscpAndPort(
    int dscp, absl::string_view port, gnmi::gNMI::StubInterface &gnmi_stub);

// Reads the name of the scheduler policy applied to the given egress port from
// the appropriate gNMI state path.
absl::StatusOr<std::string> GetSchedulerPolicyNameByEgressPort(
    absl::string_view egress_port, gnmi::gNMI::StubInterface &gnmi);

// Reads the config path of the scheduler policy of the given name.
// The config is returned unparsed as a raw JSON string.
absl::StatusOr<std::string> GetSchedulerPolicyConfig(
    absl::string_view scheduler_policy_name, gnmi::gNMI::StubInterface &gnmi);

// Updates the config path of the scheduler policy of the given name.
absl::Status UpdateSchedulerPolicyConfig(
    absl::string_view scheduler_policy_name, absl::string_view config,
    gnmi::gNMI::StubInterface &gnmi);

// Two-rate-three-color scheduler parameters. Rates are in bytes/second, sizes
// are in bytes. All parameter are optional, only non-nullopt parameters take
// effect. Documentation:
// - https://datatracker.ietf.org/doc/html/rfc2698
// - http://ops.openconfig.net/branches/models/master/docs/openconfig-qos.html
struct SchedulerParameters {
  std::optional<int64_t> committed_information_rate;  // 'cir' in OpenConfig
  std::optional<int64_t> committed_burst_size;        // 'bc' in OpenConfig
  std::optional<int64_t> peak_information_rate;       // 'pir' in OpenConfig
  std::optional<int64_t> excess_burst_size;           // 'be' in OpenConfig

  std::optional<int> weight;
};

// Updates parameters of the scheduler policy of the given name according to
// `params_by_queue_name` and waits for the updated config to converge, or times
// out with an Unavailable error if the state does not converge within the given
// `convergence_timeout`.
absl::Status SetSchedulerPolicyParameters(
    absl::string_view scheduler_policy_name,
    absl::flat_hash_map<std::string, SchedulerParameters> params_by_queue_name,
    gnmi::gNMI::StubInterface &gnmi,
    absl::Duration convergence_timeout = absl::Seconds(10));

// Reads the weights of all round-robin schedulers belonging to the given
// scheduler policy from the state path, and returns them keyed by the name of
// the queue they apply to.
absl::StatusOr<absl::flat_hash_map<std::string, int64_t>>
GetSchedulerPolicyWeightsByQueue(absl::string_view scheduler_policy_name,
                                 gnmi::gNMI::StubInterface &gnmi);

enum class QueueType {
  kStrictlyPrioritized,
  kRoundRobin,
};

struct QueueInfo {
  std::string name;
  QueueType type;
  // Priority -- queues with lower `sequence` number are scheduled first.
  int sequence = 0;
  // Meaningful only when `type == QueueType::kRoundRobin`.
  int64_t weight = 0;
};

// Reads all queues belonging to the given scheduler policy and returns their
// names and types in descending order of priority.
absl::StatusOr<std::vector<QueueInfo>>
GetQueuesForSchedulerPolicyInDescendingOrderOfPriority(
    absl::string_view scheduler_policy_name, gnmi::gNMI::StubInterface &gnmi);

// Reads all strictly prioritized queues belonging to the given scheduler policy
// from the state paths, and returns their names in descending order of
// priority.
absl::StatusOr<std::vector<std::string>>
GetStrictlyPrioritizedQueuesInDescendingOrderOfPriority(
    absl::string_view scheduler_policy_name, gnmi::gNMI::StubInterface &gnmi);

// Reads the name of the buffer allocation profile applied
// to the given egress port from the appropriate gNMI state path.
absl::StatusOr<std::string> GetBufferAllocationProfileByEgressPort(
    absl::string_view egress_port, gnmi::gNMI::StubInterface &gnmi);

// Reads the config path of the buffer profile of the given name.
// The config is returned unparsed as a raw JSON string.
absl::StatusOr<std::string> GetBufferAllocationProfileConfig(
    absl::string_view buffer_allocation_profile_name,
    gnmi::gNMI::StubInterface &gnmi);

// Updates the config path of the buffer profile of the given name.
absl::Status UpdateBufferAllocationProfileConfig(
    absl::string_view buffer_allocation_profile_name, absl::string_view config,
    gnmi::gNMI::StubInterface &gnmi);

// Queue buffer parameters.
// All parameter are optional, only non-nullopt parameters take effect.
struct BufferParameters {
  std::optional<int> dedicated_buffer;
  std::optional<bool> use_shared_buffer;
  std::optional<std::string> shared_buffer_limit_type;
  std::optional<int> dynamic_limit_scaling_factor;
  std::optional<int> shared_static_limit;
};

// Updates parameters of the buffer profile of the given name according to
// `params_by_queue_name` and waits for the updated config to converge, or times
// out with an Unavailable error if the state does not converge within the given
// `convergence_timeout`.
absl::Status SetBufferConfigParameters(
    absl::string_view buffer_allocation_profile,
    absl::flat_hash_map<std::string, BufferParameters> params_by_queue_name,
    gnmi::gNMI::StubInterface &gnmi,
    absl::Duration convergence_timeout = absl::Seconds(10));
}  // namespace pins_test

#endif  // GOOGLE_TESTS_QOS_QOS_TEST_UTIL_H_

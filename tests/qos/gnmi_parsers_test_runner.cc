#include <ostream>

#include "absl/strings/ascii.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"
#include "google/protobuf/message.h"
#include "gutil/proto.h"
#include "tests/qos/gnmi_parsers.h"

// -- Pretty priners for golden testing ----------------------------------------

namespace std {

template <class T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& ts) {
  if (ts.empty()) return os << "<empty>";
  return os << absl::StrJoin(ts, ", ", absl::StreamFormatter());
}

template <class T, class U>
std::ostream& operator<<(std::ostream& os, const std::variant<T, U>& t_or_u) {
  struct Visitor {
    std::ostream& os;
    std::ostream& operator()(const T& t) { return os << t; }
    std::ostream& operator()(const U& u) { return os << u; }
  };
  return std::visit(Visitor{.os = os}, t_or_u);
}

}  // namespace std

namespace absl {

template <class T>
std::ostream& operator<<(std::ostream& os, const absl::StatusOr<T>& statusor) {
  if (statusor.ok()) return os << *statusor;
  return os << statusor.status().code() << ": " << statusor.status().message();
}

}  // namespace absl

namespace google::protobuf {

std::ostream& operator<<(std::ostream& os, const Message& message) {
  return os << gutil::PrintTextProto(message);
}

}  // namespace google::protobuf

// -- Actual test --------------------------------------------------------------

namespace pins_test {
namespace {

static constexpr absl::string_view kInputBanner =
    "-- INPUT "
    "-----------------------------------------------------------------------\n";

static constexpr absl::string_view kTestGnmiInterfaceConfig =
    R"json(
{
   "openconfig-interfaces:interfaces" : {
      "interface" : [
          {
            "config" : {
               "name" : "Loopback0",
               "type" : "iana-if-type:softwareLoopback"
            },
            "name" : "Loopback0",
            "subinterfaces" : {
               "subinterface" : [
                  {
                     "config" : {
                        "index" : 0
                     },
                     "index" : 0,
                     "openconfig-if-ip:ipv4" : {
                        "addresses" : {
                           "address" : [
                              {
                                 "config" : {
                                    "ip" : "192.168.0.1",
                                    "prefix-length" : 27
                                 },
                                 "ip" : "192.168.0.1"
                              }
                           ]
                        }
                     },
                     "openconfig-if-ip:ipv6" : {
                        "addresses" : {
                           "address" : [
                              {
                                 "config" : {
                                    "ip" : "2607:f8b0:8096:3125::",
                                    "prefix-length" : 64
                                 },
                                 "ip" : "2607:f8b0:8096:3125::"
                              },
                              {
                                 "config" : {
                                    "ip" : "2607:f8b0:1234:5678::",
                                    "prefix-length" : 64
                                 },
                                 "ip" : "2607:f8b0:1234:5678::"
                              }
                           ]
                        }
                     }
                  }
               ]
            }
         }
      ]
   }
}
    )json";

static constexpr absl::string_view kTestGnmiQosConfig =
    R"json({
   "openconfig-qos:qos" : {
      "scheduler-policies" : {
         "scheduler-policy" : [
            {
               "config" : {
                  "name" : "scheduler_1gb"
               },
               "name" : "scheduler_1gb",
               "schedulers" : {
                  "scheduler" : [
                     {
                        "config" : {
                           "priority" : "STRICT",
                           "sequence" : 0,
                           "type" : "openconfig-qos-types:TWO_RATE_THREE_COLOR"
                        },
                        "inputs" : {
                           "input" : [
                              {
                                 "config" : {
                                    "id" : "AF4",
                                    "input-type" : "QUEUE",
                                    "queue" : "AF4"
                                 },
                                 "id" : "AF4"
                              }
                           ]
                        },
                        "sequence" : 0,
                        "two-rate-three-color" : {
                           "config" : {
                              "cir" : "0",
                              "pir" : "1000000000"
                           }
                        }
                     },
                     {
                        "config" : {
                           "priority" : "STRICT",
                           "sequence" : 1,
                           "type" : "openconfig-qos-types:TWO_RATE_THREE_COLOR"
                        },
                        "inputs" : {
                           "input" : [
                              {
                                 "config" : {
                                    "id" : "NC1",
                                    "input-type" : "QUEUE",
                                    "queue" : "NC1"
                                 },
                                 "id" : "NC1"
                              }
                           ]
                        },
                        "sequence" : 1,
                        "two-rate-three-color" : {
                           "config" : {
                              "cir" : "10000000",
                              "pir" : "20000000"
                           }
                        }
                     }
                  ]
                }
            }
        ]
      }
    }
})json";

static constexpr absl::string_view kTestGnmiQosConfig2 = R"json(
{
    "scheduler-policy": [
      {
        "name": "cpu_scheduler",
        "schedulers": {
          "scheduler": [
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF4",
                    "state": {
                      "id": "AF4",
                      "input-type": "QUEUE",
                      "queue": "AF4",
                      "weight": "0"
                    }
                  }
                ]
              },
              "sequence": 0,
              "state": {
                "priority": "STRICT",
                "sequence": 0,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "4000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "NC1",
                    "state": {
                      "id": "NC1",
                      "input-type": "QUEUE",
                      "queue": "NC1",
                      "weight": "0"
                    }
                  }
                ]
              },
              "sequence": 1,
              "state": {
                "priority": "STRICT",
                "sequence": 1,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 256,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "16000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF3",
                    "state": {
                      "id": "AF3",
                      "input-type": "QUEUE",
                      "queue": "AF3",
                      "weight": "12"
                    }
                  }
                ]
              },
              "sequence": 2,
              "state": {
                "priority": "DWRR",
                "sequence": 2,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "120"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "LLQ2",
                    "state": {
                      "id": "LLQ2",
                      "input-type": "QUEUE",
                      "queue": "LLQ2",
                      "weight": "8"
                    }
                  }
                ]
              },
              "sequence": 3,
              "state": {
                "priority": "DWRR",
                "sequence": 3,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "800"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF1",
                    "state": {
                      "id": "AF1",
                      "input-type": "QUEUE",
                      "queue": "AF1",
                      "weight": "4"
                    }
                  }
                ]
              },
              "sequence": 4,
              "state": {
                "priority": "DWRR",
                "sequence": 4,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "120"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF2",
                    "state": {
                      "id": "AF2",
                      "input-type": "QUEUE",
                      "queue": "AF2",
                      "weight": "4"
                    }
                  }
                ]
              },
              "sequence": 5,
              "state": {
                "priority": "DWRR",
                "sequence": 5,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "800"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "BE1",
                    "state": {
                      "id": "BE1",
                      "input-type": "QUEUE",
                      "queue": "BE1",
                      "weight": "1"
                    }
                  }
                ]
              },
              "sequence": 6,
              "state": {
                "priority": "DWRR",
                "sequence": 6,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "120"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "LLQ1",
                    "state": {
                      "id": "LLQ1",
                      "input-type": "QUEUE",
                      "queue": "LLQ1",
                      "weight": "1"
                    }
                  }
                ]
              },
              "sequence": 7,
              "state": {
                "priority": "DWRR",
                "sequence": 7,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "google-pins-qos:bc-pkts": 0,
                  "google-pins-qos:be-pkts": 4,
                  "google-pins-qos:cir-pkts": "0",
                  "google-pins-qos:pir-pkts": "800"
                }
              }
            }
          ]
        },
        "state": {
          "name": "cpu_scheduler"
        }
      },
      {
        "name": "scheduler_100gb",
        "schedulers": {
          "scheduler": [
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF4",
                    "state": {
                      "id": "AF4",
                      "input-type": "QUEUE",
                      "queue": "AF4",
                      "weight": "0"
                    }
                  }
                ]
              },
              "sequence": 0,
              "state": {
                "priority": "STRICT",
                "sequence": 0,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "NC1",
                    "state": {
                      "id": "NC1",
                      "input-type": "QUEUE",
                      "queue": "NC1",
                      "weight": "0"
                    }
                  }
                ]
              },
              "sequence": 1,
              "state": {
                "priority": "STRICT",
                "sequence": 1,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "1000000000",
                  "pir": "2000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF3",
                    "state": {
                      "id": "AF3",
                      "input-type": "QUEUE",
                      "queue": "AF3",
                      "weight": "12"
                    }
                  }
                ]
              },
              "sequence": 2,
              "state": {
                "priority": "DWRR",
                "sequence": 2,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "LLQ2",
                    "state": {
                      "id": "LLQ2",
                      "input-type": "QUEUE",
                      "queue": "LLQ2",
                      "weight": "8"
                    }
                  }
                ]
              },
              "sequence": 3,
              "state": {
                "priority": "DWRR",
                "sequence": 3,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF1",
                    "state": {
                      "id": "AF1",
                      "input-type": "QUEUE",
                      "queue": "AF1",
                      "weight": "4"
                    }
                  }
                ]
              },
              "sequence": 4,
              "state": {
                "priority": "DWRR",
                "sequence": 4,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "AF2",
                    "state": {
                      "id": "AF2",
                      "input-type": "QUEUE",
                      "queue": "AF2",
                      "weight": "4"
                    }
                  }
                ]
              },
              "sequence": 5,
              "state": {
                "priority": "DWRR",
                "sequence": 5,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "BE1",
                    "state": {
                      "id": "BE1",
                      "input-type": "QUEUE",
                      "queue": "BE1",
                      "weight": "1"
                    }
                  }
                ]
              },
              "sequence": 6,
              "state": {
                "priority": "DWRR",
                "sequence": 6,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            },
            {
              "inputs": {
                "input": [
                  {
                    "id": "LLQ1",
                    "state": {
                      "id": "LLQ1",
                      "input-type": "QUEUE",
                      "queue": "LLQ1",
                      "weight": "1"
                    }
                  }
                ]
              },
              "sequence": 7,
              "state": {
                "priority": "DWRR",
                "sequence": 7,
                "type": "openconfig-qos-types:TWO_RATE_THREE_COLOR"
              },
              "two-rate-three-color": {
                "state": {
                  "bc": 0,
                  "be": 0,
                  "cir": "0",
                  "pir": "100000000000"
                }
              }
            }
          ]
        },
        "state": {
          "name": "scheduler_100gb"
        }
      }
    ]
}
)json";

void RunAllParsersAndPrintTheirOutput() {
  std::cout << kInputBanner
            << absl::StripAsciiWhitespace(kTestGnmiInterfaceConfig) << "\n";

  // Test ParseLoopbackIps.
  std::cout << "-- OUTPUT: ParseLoopbackIps --\n";
  std::cout << ParseLoopbackIps(kTestGnmiInterfaceConfig) << "\n";

  // Test ParseLoopbackIpv4s.
  std::cout << "-- OUTPUT: ParseLoopbackIpv4s --\n";
  std::cout << ParseLoopbackIpv4s(kTestGnmiInterfaceConfig) << "\n";

  // Test ParseLoopbackIpv6s.
  std::cout << "-- OUTPUT: ParseLoopbackIpv6s --\n";
  std::cout << ParseLoopbackIpv6s(kTestGnmiInterfaceConfig) << "\n";

  // Test openconfig::Qos parsing.
  std::cout << kInputBanner << absl::StripAsciiWhitespace(kTestGnmiQosConfig)
            << "\n";
  std::cout << "-- OUTPUT: gutil::ParseJsonAsProto<openconfig::Config> --\n";
  std::cout << gutil::ParseJsonAsProto<openconfig::Config>(
                   kTestGnmiQosConfig,
                   /*ignore_unknown_fields=*/true)
            << "\n";

  // Test openconfig::Qos parsing.
  std::cout << kInputBanner << absl::StripAsciiWhitespace(kTestGnmiQosConfig2)
            << "\n";
  std::cout << "-- OUTPUT: gutil::ParseJsonAsProto<openconfig::Config> --\n";
  std::cout << gutil::ParseJsonAsProto<openconfig::Config>(
                   kTestGnmiQosConfig,
                   /*ignore_unknown_fields=*/true)
            << "\n";
}

}  // namespace
}  // namespace pins_test

int main() { pins_test::RunAllParsersAndPrintTheirOutput(); }

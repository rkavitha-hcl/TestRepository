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

// Defines our SymbolicGuardedMap class.

#include "p4_symbolic/symbolic/guarded_map.h"

#include "absl/container/btree_map.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "p4_symbolic/symbolic/operators.h"
#include "p4_symbolic/symbolic/util.h"

namespace p4_symbolic {
namespace symbolic {

absl::StatusOr<SymbolicGuardedMap> SymbolicGuardedMap::CreateSymbolicGuardedMap(
    const google::protobuf::Map<std::string, ir::HeaderType> &headers) {
  ASSIGN_OR_RETURN(auto map, util::FreeSymbolicHeaders(headers));
  return SymbolicGuardedMap(map);
}

bool SymbolicGuardedMap::ContainsKey(const std::string &key) const {
  return this->map_.count(key) == 1;
}

absl::StatusOr<z3::expr> SymbolicGuardedMap::Get(const std::string &key) const {
  if (this->ContainsKey(key)) {
    return this->map_.at(key);
  }

  return absl::InvalidArgumentError(
      absl::StrCat("Cannot find key \"", key, "\" in SymbolicGuardedMap!"));
}

absl::Status SymbolicGuardedMap::Set(const std::string &key, z3::expr value,
                                     const z3::expr &guard) {
  if (!this->ContainsKey(key)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Cannot assign to key \"", key, "\" in SymbolicGuardedMap!"));
  }

  z3::expr &old_value = this->map_.at(key);

  // Ite will pad bitvectors to the same size, but this is not the right
  // semantics if we assign a larger bitvector into a smaller one. Instead, the
  // assigned value needs to be truncated to the bitsize of the asignee.
  if (old_value.get_sort().is_bv() && value.get_sort().is_bv() &&
      old_value.get_sort().bv_size() < value.get_sort().bv_size()) {
    value = value.extract(old_value.get_sort().bv_size() - 1, 0);
  }

  // This will return an absl error if the sorts are incompatible, and will pad
  // shorter bit vectors.
  ASSIGN_OR_RETURN(old_value, operators::Ite(guard, value, old_value));
  return absl::OkStatus();
}

}  // namespace symbolic
}  // namespace p4_symbolic

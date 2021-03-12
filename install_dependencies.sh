#!/bin/bash

# Copyright 2021 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# TODO: Avoid system dependencies like libnl, currently these are
# coming because Sonic swss common depends on them.
sudo apt-get update
sudo apt-get install bison flex libfl-dev libgmp-dev libhiredis-dev libnl-3-dev libnl-genl-3-dev libnl-route-3-dev libnl-nf-3-dev libboost-dev


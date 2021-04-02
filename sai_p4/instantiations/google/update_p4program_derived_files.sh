#!/bin/bash

# Checks that the P4 program has been updated correctly by running all P4
# program tests and updating all files that are directly derived from the P4
# program such as P4Infos or PD protos.

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
cd $DIR

# Run all things that need updating.
bazel query :all | grep _up_to_date_test | while read target; do
  bazel run $target -- --update
done

# Check P4 program.
bazel test :sai_p4info_test

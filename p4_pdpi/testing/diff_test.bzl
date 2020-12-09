# Copyright 2020 Google LLC
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

"""Blaze targets for golden file testing.

This file defines targets `diff_test` and `cmd_diff_test` for "golden file
testing".

The diff_test target computes the diff of some `actual` output and some
`expected` output, either succeeding if the diff is empty or failing and
printing the nonempty diff otherwise. To auto-generate or update the expected
file, run:
```sh
    bazel run <diff test target> -- --update`
```
Make sure that the expected file exists.
"""

def execpath(path):
    return "$(execpath %s)" % path

def rootpath(path):
    return "$(rootpath %s)" % path

def _diff_test_script(ctx):
    """Returns bash script to be executed by the diff_test target."""
    return """
if [[ "$1" == "--update" ]]; then
    cp -f "{actual}" "${{BUILD_WORKSPACE_DIRECTORY}}/{expected}"
fi

diff -u "{expected}" "{actual}"

if [[ $? = 0 ]]; then
    # Expected and actual agree.
    if [[ "$1" == "--update" ]]; then
        echo "Successfully updated: {expected}."
    else
        echo "PASSED"
    fi
    exit 0
else
    # Expected and actual disagree.
    if [[ "$1" == "--update" ]]; then
        echo "Failed to update: {expected}. Try updating manually."
    else
        cat << EOF

Output not as expected. To update $(basename {expected}), run the following command:
bazel run {target} -- --update
EOF
    fi
    exit 1
fi
    """.format(
        actual = ctx.file.actual.short_path,
        expected = ctx.file.expected.short_path,
        target = ctx.label,
    )

def _diff_test_impl(ctx):
    """Computes diff of two files, checking that they agree.

    When invoked as `bazel run <target> -- --update`, will update the `expected`
    file to match the contents of the `actual` file. Note that the file must
    already exist.
    """

    # Write test script that will be executed by 'bazel test'.
    ctx.actions.write(
        output = ctx.outputs.executable,
        content = _diff_test_script(ctx),
        is_executable = True,
    )

    # Make test script dependencies available at runtime.
    runfiles = [ctx.file.actual, ctx.file.expected]
    return DefaultInfo(
        runfiles = ctx.runfiles(files = runfiles),
    )

diff_test = rule(
    doc = """Computes diff of two files, checking that they agree.

    Typically used to test that the output of some command looks as expected.
    To update the expected file, run `bazel run <target> -- --update`.
    """,
    implementation = _diff_test_impl,
    test = True,
    attrs = {
        "actual": attr.label(
            doc = "'Actual' file, typically containing the output of some command.",
            mandatory = True,
            allow_single_file = True,
        ),
        "expected": attr.label(
            doc = """\
Expected file (aka golden file), containing the expected output.
To auto-generate or update, run `bazel run <target> -- --update`.
""",
            mandatory = True,
            allow_single_file = True,
        ),
    },
)

def cmd_diff_test(name, actual_cmd, expected, tools = [], visibility = None):
    """Runs a command to get the actual output, to compare against `expected`."""
    native.genrule(
        name = name + "_output",
        visibility = visibility,
        srcs = [],
        outs = [name + ".actual"],
        tools = tools,
        cmd = actual_cmd + " > '$@'",
        testonly = True,
    )
    diff_test(
        name = name,
        actual = name + ".actual",
        expected = expected,
    )

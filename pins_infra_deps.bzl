"""Third party dependencies."""

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository", "new_git_repository")
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def pins_infra_deps():
    """Sets up 3rd party workspaces needed to build PINS infrastructure."""
    if not native.existing_rule("com_google_absl"):
        http_archive(
            name = "com_google_absl",
            url = "https://github.com/abseil/abseil-cpp/archive/20200923.tar.gz",
            strip_prefix = "abseil-cpp-20200923",
            sha256 = "b3744a4f7a249d5eaf2309daad597631ce77ea62e0fc6abffbab4b4c3dc0fc08",
        )
    if not native.existing_rule("com_google_googletest"):
        http_archive(
            name = "com_google_googletest",
            urls = ["https://github.com/google/googletest/archive/release-1.10.0.tar.gz"],
            strip_prefix = "googletest-release-1.10.0",
            sha256 = "9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb",
        )
    if not native.existing_rule("com_google_protobuf"):
        http_archive(
            name = "com_google_protobuf",
            url = "https://github.com/protocolbuffers/protobuf/releases/download/v3.12.3/protobuf-all-3.12.3.tar.gz",
            strip_prefix = "protobuf-3.12.3",
            sha256 = "1a83f0525e5c8096b7b812181865da3c8637de88f9777056cefbf51a1eb0b83f",
        )
    if not native.existing_rule("com_googlesource_code_re2"):
        http_archive(
            name = "com_googlesource_code_re2",
            url = "https://github.com/google/re2/archive/2020-07-06.tar.gz",
            strip_prefix = "re2-2020-07-06",
            sha256 = "2e9489a31ae007c81e90e8ec8a15d62d58a9c18d4fd1603f6441ef248556b41f",
        )
    if not native.existing_rule("com_google_googleapis"):
        git_repository(
            name = "com_google_googleapis",
            commit = "dd244bb3a5023a4a9290b21dae6b99020c026123",
            remote = "https://github.com/googleapis/googleapis",
            shallow_since = "1591402163 -0700",
        )
    if not native.existing_rule("com_github_google_glog"):
        http_archive(
            name = "com_github_google_glog",
            url = "https://github.com/google/glog/archive/v0.4.0.tar.gz",
            strip_prefix = "glog-0.4.0",
            sha256 = "f28359aeba12f30d73d9e4711ef356dc842886968112162bc73002645139c39c",
        )

    # Needed to make glog happy.
    if not native.existing_rule("com_github_gflags_gflags"):
        http_archive(
            name = "com_github_gflags_gflags",
            url = "https://github.com/gflags/gflags/archive/v2.2.2.tar.gz",
            strip_prefix = "gflags-2.2.2",
            sha256 = "34af2f15cf7367513b352bdcd2493ab14ce43692d2dcd9dfc499492966c64dcf",
        )
    if not native.existing_rule("com_github_gnmi"):
        git_repository(
            name = "com_github_gnmi",
            commit = "6a51fc9396af4bb9d7b3c4cf6e9718b81ba77e80",
            # TODO: Upstream changes from this private repo to official gnmi repo.
            remote = "https://github.com/vamsipunati/gnmi.git",
        )
    if not native.existing_rule("rules_cc"):
        git_repository(
            name = "rules_cc",
            commit = "1477dbab59b401daa94acedbeaefe79bf9112167",
            remote = "https://github.com/bazelbuild/rules_cc.git",
            shallow_since = "1595949469 -0700",
        )
    if not native.existing_rule("rules_proto"):
        http_archive(
            name = "rules_proto",
            urls = [
                "https://mirror.bazel.build/github.com/bazelbuild/rules_proto/archive/97d8af4dc474595af3900dd85cb3a29ad28cc313.tar.gz",
                "https://github.com/bazelbuild/rules_proto/archive/97d8af4dc474595af3900dd85cb3a29ad28cc313.tar.gz",
            ],
            strip_prefix = "rules_proto-97d8af4dc474595af3900dd85cb3a29ad28cc313",
            sha256 = "602e7161d9195e50246177e7c55b2f39950a9cf7366f74ed5f22fd45750cd208",
        )
    if not native.existing_rule("com_github_p4lang_p4c"):
        git_repository(
            name = "com_github_p4lang_p4c",
            # Newest commit on master on 2020-09-10.
            commit = "557b77f8c41fc5ee1158710eda1073d84f5acf53",
            remote = "https://github.com/p4lang/p4c",
            shallow_since = "1599773698 -0700",
        )
    if not native.existing_rule("com_github_p4lang_p4runtime"):
        http_archive(
            name = "com_github_p4lang_p4runtime",
            urls = ["https://github.com/p4lang/p4runtime/archive/v1.3.0.tar.gz"],
            strip_prefix = "p4runtime-1.3.0/proto",
            sha256 = "09d826e868b1c18e47ff1b5c3d9c6afc5fa7b7a3f856f9d2d9273f38f4fc87e2",
        )
    if not native.existing_rule("com_github_p4lang_p4_constraints"):
        git_repository(
            name = "com_github_p4lang_p4_constraints",
            # Newest commit on master on 2020-12-21.
            commit = "cbd911c54e5d21ce8448865366263a6915a7e434",
            remote = "https://github.com/p4lang/p4-constraints",
        )
    if not native.existing_rule("com_github_nlohmann_json"):
        http_archive(
            name = "com_github_nlohmann_json",
            # JSON for Modern C++
            url = "https://github.com/nlohmann/json/archive/v3.7.3.zip",
            strip_prefix = "json-3.7.3",
            sha256 = "e109cd4a9d1d463a62f0a81d7c6719ecd780a52fb80a22b901ed5b6fe43fb45b",
            build_file_content = """cc_library(name="json",
                                               visibility=["//visibility:public"],
                                               hdrs=["single_include/nlohmann/json.hpp"]
                                              )""",
        )
    if not native.existing_rule("com_github_ivmai_cudd"):
        http_archive(
            name = "com_github_ivmai_cudd",
            build_file = "@//:bazel/BUILD.cudd.bazel",
            strip_prefix = "cudd-cudd-3.0.0",
            sha256 = "5fe145041c594689e6e7cf4cd623d5f2b7c36261708be8c9a72aed72cf67acce",
            urls = ["https://github.com/ivmai/cudd/archive/cudd-3.0.0.tar.gz"],
        )
    if not native.existing_rule("com_google_protobuf_mutator"):
        new_git_repository(
            name = "com_github_google_libprotobuf_mutator",
            branch = "master",
            build_file = "@//:bazel/BUILD.libprotobuf_mutator.bazel",
            remote = "https://github.com/google/libprotobuf-mutator.git",
        )
    if not native.existing_rule("rules_foreign_cc"):
        http_archive(
            name = "rules_foreign_cc",
            strip_prefix = "rules_foreign_cc-master",
            url = "https://github.com/bazelbuild/rules_foreign_cc/archive/master.zip",
        )
    if not native.existing_rule("com_gnu_gmp"):
        http_archive(
            name = "com_gnu_gmp",
            url = "https://gmplib.org/download/gmp/gmp-6.1.2.tar.xz",
            strip_prefix = "gmp-6.1.2",
            sha256 = "87b565e89a9a684fe4ebeeddb8399dce2599f9c9049854ca8c0dfbdea0e21912",
            build_file = "@//:bazel/BUILD.gmp.bazel",
        )

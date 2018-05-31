# Description:
#   Bazel workspace file for Brotli.

workspace(name = "org_brotli")

maven_jar(
    name = "junit_junit",
    artifact = "junit:junit:4.12",
)

http_archive(
    name = "io_bazel_rules_go",
    urls = ["https://github.com/bazelbuild/rules_go/releases/download/0.12.0/rules_go-0.12.0.tar.gz"],
    sha256 = "c1f52b8789218bb1542ed362c4f7de7052abcf254d865d96fb7ba6d44bc15ee3",
)

http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "a80acb69c63d5f6437b099c111480a4493bad4592015af2127a2f49fb7512d8d",
    strip_prefix = "rules_closure-0.7.0",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_closure/archive/0.7.0.tar.gz",
        "https://github.com/bazelbuild/rules_closure/archive/0.7.0.tar.gz",
    ],
)

new_http_archive(
    name = "openjdk_linux",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
    ],
    sha256 = "7e6284739c0e5b7142bc7a9adc61ced70dc5bb26b130b582b18e809013bcb251",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-linux_x64/include/jni.h"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-linux_x64/include/linux/jni_md.h"],
)""",
)

new_http_archive(
    name = "openjdk_macos",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
    ],
    sha256 = "ff533364c9cbd3b271ab5328efe28e2dd6d7bae5b630098a5683f742ecf0709d",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-macosx_x64/include/darwin/jni_md.h"],
)""",
)

new_http_archive(
    name = "openjdk_win",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
    ],
    sha256 = "f1d9d3341ef7c8c9baff3597953e99a6a7c64f8608ee62c03fdd7574b7655c02",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-win_x64/include/win32/jni_md.h"],
)""",
)

new_local_repository(
    name = "divsufsort",
    build_file = "//research:BUILD.libdivsufsort",
    path = "research/libdivsufsort",
)

load("@io_bazel_rules_closure//closure:defs.bzl", "closure_repositories")
closure_repositories()

load("@io_bazel_rules_go//go:def.bzl", "go_rules_dependencies", "go_register_toolchains")
go_rules_dependencies()
go_register_toolchains()

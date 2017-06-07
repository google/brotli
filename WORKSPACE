# Description:
#   Bazel workspace file for Brotli.

workspace(name = "org_brotli")

maven_jar(
    name = "junit_junit",
    artifact = "junit:junit:4.12",
)

git_repository(
    name = "io_bazel_rules_go",
    remote = "https://github.com/bazelbuild/rules_go.git",
    tag = "0.4.4",
)

new_http_archive(
    name = "openjdk_linux",
    url = "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.20.0.5-jdk8.0.121/zulu8.20.0.5-jdk8.0.121-linux_x64.tar.gz",
    sha256 = "7fdfb17d890406470b2303d749d3138e7f353749e67a0a22f542e1ab3e482745",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_h",
    srcs = ["zulu8.20.0.5-jdk8.0.121-linux_x64/include/jni.h"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.20.0.5-jdk8.0.121-linux_x64/include/linux/jni_md.h"],
)""",
)

new_http_archive(
    name = "openjdk_macos",
    url = "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.20.0.5-jdk8.0.121/zulu8.20.0.5-jdk8.0.121-macosx_x64.zip",
    sha256 = "2a58bd1d9b0cbf0b3d8d1bcdd117c407e3d5a0ec01e2f53565c9bec5cf9ea78b",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.20.0.5-jdk8.0.121-macosx_x64/include/darwin/jni_md.h"],
)""",
)

load("@io_bazel_rules_go//go:def.bzl", "go_repositories")
go_repositories()

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
    tag = "0.4.1",
)
load("@io_bazel_rules_go//go:def.bzl", "go_repositories")

go_repositories()

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

load("@io_bazel_rules_go//go:def.bzl", "go_prefix", "go_library", "go_test")

go_prefix("github.com/google/brotli")

go_library(
    name = "cbrotli",
    srcs = [
        "reader.go",
        "writer.go",
    ],
    cdeps = [
        "//:brotlidec",
        "//:brotlienc",
    ],
    cgo=True,
)

go_test(
    name = "cbrotli_test",
    size = "small",
    srcs = ["cbrotli_test.go"],
    embed = [":cbrotli"],
)

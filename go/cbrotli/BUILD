package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

load("@io_bazel_rules_go//go:def.bzl", "go_prefix", "cgo_library", "go_test")

go_prefix("github.com/google/brotli")

cgo_library(
    name = "cbrotli",
    srcs = [
        "reader.go",
        "writer.go",
    ],
    cdeps = [
        "//:brotlidec",
        "//:brotlienc",
    ],
)

go_test(
    name = "cbrotli_test",
    size = "small",
    srcs = ["cbrotli_test.go"],
    library = ":cbrotli",
)

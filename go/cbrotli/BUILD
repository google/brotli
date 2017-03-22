package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

load("@io_bazel_rules_go//go:def.bzl", "go_prefix", "go_library", "go_test")

go_prefix("github.com/google/brotli")

go_library(
    name = "cbrotli",
    srcs = ["cbrotli.go"],
    deps = [
        "//go/cbrotli/internal:decoder",
        "//go/cbrotli/internal:encoder",
    ],
)

go_test(
    name = "cbrotli_test",
    size = "small",
    srcs = ["cbrotli_test.go"],
    library = ":cbrotli",
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

load("@io_bazel_rules_go//go:def.bzl", "cgo_library")

cgo_library(
    name = "decoder",
    srcs = ["decoder.go"],
    visibility = ["//go/cbrotli:__subpackages__"],
    cdeps = ["//:brotlidec"],
)

cgo_library(
    name = "encoder",
    srcs = ["encoder.go"],
    visibility = ["//go/cbrotli:__subpackages__"],
    cdeps = ["//:brotlienc"],
)

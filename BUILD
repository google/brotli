# Description:
#   Brotli is a generic-purpose lossless compression algorithm.

package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # MIT

exports_files(["LICENSE"])

STRICT_C_OPTIONS = [
    "--pedantic-errors",
    "-Wall",
    "-Wconversion",
    "-Werror",
    "-Wextra",
    "-Wlong-long",
    "-Wmissing-declarations",
    "-Wmissing-prototypes",
    "-Wno-strict-aliasing",
    "-Wshadow",
    "-Wsign-compare",
]

filegroup(
    name = "public_headers",
    srcs = glob(["c/include/brotli/*.h"]),
)

filegroup(
    name = "common_headers",
    srcs = glob(["c/common/*.h"]),
)

filegroup(
    name = "common_sources",
    srcs = glob(["c/common/*.c"]),
)

filegroup(
    name = "dec_headers",
    srcs = glob(["c/dec/*.h"]),
)

filegroup(
    name = "dec_sources",
    srcs = glob(["c/dec/*.c"]),
)

filegroup(
    name = "enc_headers",
    srcs = glob(["c/enc/*.h"]),
)

filegroup(
    name = "enc_sources",
    srcs = glob(["c/enc/*.c"]),
)

cc_library(
    name = "brotli",
    hdrs = [":public_headers"],
    copts = STRICT_C_OPTIONS,
    includes = ["c/include"],
)

cc_library(
    name = "brotlicommon",
    srcs = [":common_sources"],
    hdrs = [":common_headers"],
    copts = STRICT_C_OPTIONS,
    deps = [":brotli"],
)

cc_library(
    name = "brotlidec",
    srcs = [":dec_sources"],
    hdrs = [":dec_headers"],
    copts = STRICT_C_OPTIONS,
    deps = [":brotlicommon"],
)

cc_library(
    name = "brotlienc",
    srcs = [":enc_sources"],
    hdrs = [":enc_headers"],
    copts = STRICT_C_OPTIONS,
    linkopts = ["-lm"],
    deps = [":brotlicommon"],
)

cc_binary(
    name = "bro",
    srcs = ["c/tools/bro.c"],
    copts = STRICT_C_OPTIONS,
    linkstatic = 1,
    deps = [
        ":brotlidec",
        ":brotlienc",
    ],
)

load("@io_bazel_rules_go//go:def.bzl", "go_prefix")

go_prefix("github.com/google/brotli")

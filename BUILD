# Description:
#   Brotli is a generic-purpose lossless compression algorithm.

package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # MIT

exports_files(["LICENSE"])

# >>> JNI headers

config_setting(
    name = "darwin",
    values = {"cpu": "darwin"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "darwin_x86_64",
    values = {"cpu": "darwin_x86_64"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "windows",
    values = {"cpu": "x64_windows"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "windows_msvc",
    values = {"cpu": "x64_windows_msvc"},
    visibility = ["//visibility:public"],
)

config_setting(
    name = "windows_msys",
    values = {"cpu": "x64_windows_msys"},
    visibility = ["//visibility:public"],
)

genrule(
    name = "copy_link_jni_header",
    srcs = ["@openjdk_linux//:jni_h"],
    outs = ["jni/jni.h"],
    cmd = "cp -f $< $@",
)

genrule(
    name = "copy_link_jni_md_header",
    srcs = select({
        ":darwin": ["@openjdk_macos//:jni_md_h"],
        ":darwin_x86_64": ["@openjdk_macos//:jni_md_h"],
        ":windows_msys": ["@openjdk_win//:jni_md_h"],
        ":windows_msvc": ["@openjdk_win//:jni_md_h"],
        ":windows": ["@openjdk_win//:jni_md_h"],
        "//conditions:default": ["@openjdk_linux//:jni_md_h"],
    }),
    outs = ["jni/jni_md.h"],
    cmd = "cp -f $< $@",
)

cc_library(
    name = "jni_inc",
    hdrs = [
        ":jni/jni.h",
        ":jni/jni_md.h",
    ],
    includes = ["jni"],
)

# <<< JNI headers

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
    name = "brotli_inc",
    hdrs = [":public_headers"],
    copts = STRICT_C_OPTIONS,
    includes = ["c/include"],
)

cc_library(
    name = "brotlicommon",
    srcs = [":common_sources"],
    hdrs = [":common_headers"],
    copts = STRICT_C_OPTIONS,
    deps = [":brotli_inc"],
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
    name = "brotli",
    srcs = ["c/tools/brotli.c"],
    copts = STRICT_C_OPTIONS,
    linkstatic = 1,
    deps = [
        ":brotlidec",
        ":brotlienc",
    ],
)

########################################################
# WARNING: do not (transitively) depend on this target!
########################################################
cc_binary(
    name = "brotli_jni.dll",
    srcs = [
        ":common_headers",
        ":common_sources",
        ":dec_headers",
        ":dec_sources",
        ":enc_headers",
        ":enc_sources",
        "//java/org/brotli/wrapper/common:jni_src",
        "//java/org/brotli/wrapper/dec:jni_src",
        "//java/org/brotli/wrapper/enc:jni_src",
    ],
    deps = [
        ":brotli_inc",
        ":jni_inc",
    ],
    linkshared = 1,
)

########################################################
# WARNING: do not (transitively) depend on this target!
########################################################
cc_binary(
    name = "brotli_jni_no_dictionary_data.dll",
    srcs = [
        ":common_headers",
        ":common_sources",
        ":dec_headers",
        ":dec_sources",
        ":enc_headers",
        ":enc_sources",
        "//java/org/brotli/wrapper/common:jni_src",
        "//java/org/brotli/wrapper/dec:jni_src",
        "//java/org/brotli/wrapper/enc:jni_src",
    ],
    defines = [
        "BROTLI_EXTERNAL_DICTIONARY_DATA=",
    ],
    deps = [
        ":brotli_inc",
        ":jni_inc",
    ],
    linkshared = 1,
)

filegroup(
    name = "dictionary",
    srcs = ["c/common/dictionary.bin"],
)

load("@io_bazel_rules_go//go:def.bzl", "go_prefix")

go_prefix("github.com/google/brotli")

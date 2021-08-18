# Description:
#   Java port of Brotli decoder.

load(":build_defs.bzl", "brotli_java_test")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

TEST_DEPS = [
    ":dec",
    "@maven//:junit_junit",
]

java_library(
    name = "dec",
    srcs = glob(
        ["*.java"],
        exclude = [
            "Decoder.java",
            "*Test*.java",
        ],
    ),
    proguard_specs = ["proguard.pgcfg"],
    resource_jars = ["//:license"],
)

brotli_java_test(
    name = "BitReaderTest",
    srcs = ["BitReaderTest.java"],
    deps = TEST_DEPS,
)

brotli_java_test(
    name = "DecodeTest",
    srcs = ["DecodeTest.java"],
    deps = TEST_DEPS,
)

brotli_java_test(
    name = "DictionaryTest",
    srcs = ["DictionaryTest.java"],
    deps = TEST_DEPS,
)

brotli_java_test(
    name = "EagerStreamTest",
    srcs = ["EagerStreamTest.java"],
    deps = TEST_DEPS,
)

brotli_java_test(
    name = "SynthTest",
    srcs = ["SynthTest.java"],
    deps = TEST_DEPS,
)

brotli_java_test(
    name = "TransformTest",
    srcs = ["TransformTest.java"],
    deps = TEST_DEPS,
)

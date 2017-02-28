# Description:
#   Java port of Brotli decoder.

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

java_library(
    name = "lib",
    srcs = glob(["*.java"], exclude = ["*Test*.java"]),
)

java_library(
    name = "test_lib",
    srcs = glob(["*Test*.java"]),
    deps = [
        ":lib",
        "@junit_junit//jar",
    ],
    testonly = 1,
)

java_test(
    name = "BitReaderTest",
    test_class = "org.brotli.dec.BitReaderTest",
    runtime_deps = [":test_lib"],
)

java_test(
    name = "DecodeTest",
    test_class = "org.brotli.dec.DecodeTest",
    runtime_deps = [":test_lib"],
)

java_test(
    name = "DictionaryTest",
    test_class = "org.brotli.dec.DictionaryTest",
    runtime_deps = [":test_lib"],
)

java_test(
    name = "SynthTest",
    test_class = "org.brotli.dec.SynthTest",
    runtime_deps = [":test_lib"],
)

java_test(
    name = "TransformTest",
    test_class = "org.brotli.dec.TransformTest",
    runtime_deps = [":test_lib"],
)

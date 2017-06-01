package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["decoder_jni.cc"],
)

#########################################
# WARNING: do not depend on this target!
#########################################
java_library(
    name = "dec",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
    deps = ["//:jni"],
)

filegroup(
    name = "test_bundle",
    srcs = ["//java/org/brotli/integration:test_data"],
)

java_test(
    name = "BrotliDecoderChannelTest",
    size = "large",
    srcs = ["BrotliDecoderChannelTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    deps = [
        ":dec",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "BrotliInputStreamTest",
    size = "large",
    srcs = ["BrotliInputStreamTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    deps = [
        ":dec",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "DecoderTest",
    size = "large",
    srcs = ["DecoderTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    deps = [
        ":dec",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

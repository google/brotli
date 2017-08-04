package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["encoder_jni.cc"],
)

#########################################
# WARNING: do not depend on this target!
#########################################
java_library(
    name = "enc",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
    deps = ["//:jni"],
)

filegroup(
    name = "test_bundle",
    srcs = ["//java/org/brotli/integration:test_corpus"],
)

java_test(
    name = "BrotliEncoderChannelTest",
    size = "large",
    srcs = ["BrotliEncoderChannelTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:bundle_helper",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "BrotliOutputStreamTest",
    size = "large",
    srcs = ["BrotliOutputStreamTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:bundle_helper",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "EncoderTest",
    size = "large",
    srcs = ["EncoderTest.java"],
    data = [
        ":test_bundle",
        "//:jni",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DTEST_BUNDLE=$(location :test_bundle)"],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:bundle_helper",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

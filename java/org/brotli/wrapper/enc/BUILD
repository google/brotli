package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["encoder_jni.cc"],
)

filegroup(
    name = "brotli_jni",
    srcs = ["//:brotli_jni.dll"],
)

java_library(
    name = "enc",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
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
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:brotli_jni_test_base",
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
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:brotli_jni_test_base",
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
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    shard_count = 15,
    deps = [
        ":enc",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/integration:bundle_helper",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["decoder_jni.cc"],
)

java_library(
    name = "dec",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
)

filegroup(
    name = "brotli_jni",
    srcs = ["//:brotli_jni.dll"],
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
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    deps = [
        ":dec",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "BrotliInputStreamTest",
    size = "large",
    srcs = ["BrotliInputStreamTest.java"],
    data = [
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    deps = [
        ":dec",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

java_test(
    name = "DecoderTest",
    size = "large",
    srcs = ["DecoderTest.java"],
    data = [
        ":brotli_jni",  # Bazel JNI workaround
        ":test_bundle",
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni)",
        "-DTEST_BUNDLE=$(location :test_bundle)",
    ],
    deps = [
        ":dec",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/integration:bundle_helper",
        "@junit_junit//jar",
    ],
)

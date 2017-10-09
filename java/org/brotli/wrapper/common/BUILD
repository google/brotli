package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["common_jni.cc"],
)

filegroup(
    name = "brotli_jni_no_dictionary_data",
    srcs = ["//:brotli_jni_no_dictionary_data.dll"],
)

java_library(
    name = "common",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
)

java_test(
    name = "SetZeroDictionaryTest",
    size = "small",
    srcs = ["SetZeroDictionaryTest.java"],
    data = [
        ":brotli_jni_no_dictionary_data",  # Bazel JNI workaround
    ],
    jvm_flags = [
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni_no_dictionary_data)",
    ],
    deps = [
        ":common",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

filegroup(
    name = "rfc_dictionary",
    srcs = ["//:dictionary"],
)

java_test(
    name = "SetRfcDictionaryTest",
    size = "small",
    srcs = ["SetRfcDictionaryTest.java"],
    data = [
        ":rfc_dictionary",
        ":brotli_jni_no_dictionary_data",  # Bazel JNI workaround
    ],
    jvm_flags = [
        "-DRFC_DICTIONARY=$(location :rfc_dictionary)",
        "-DBROTLI_JNI_LIBRARY=$(location :brotli_jni_no_dictionary_data)",
    ],
    deps = [
        ":common",
        "//java/org/brotli/integration:brotli_jni_test_base",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

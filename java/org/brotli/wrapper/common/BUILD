package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

filegroup(
    name = "jni_src",
    srcs = ["common_jni.cc"],
)

#########################################
# WARNING: do not depend on this target!
#########################################
java_library(
    name = "common_no_dictionary_data",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
    deps = ["//:jni_no_dictionary_data"],
)

#########################################
# WARNING: do not depend on this target!
#########################################
java_library(
    name = "common",
    srcs = glob(
        ["*.java"],
        exclude = ["*Test*.java"],
    ),
    deps = ["//:jni"],
)

java_test(
    name = "SetZeroDictionaryTest",
    size = "small",
    srcs = ["SetZeroDictionaryTest.java"],
    data = ["//:jni_no_dictionary_data"],  # Bazel JNI workaround
    deps = [
        ":common_no_dictionary_data",
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
        "//:jni_no_dictionary_data",  # Bazel JNI workaround
    ],
    jvm_flags = ["-DRFC_DICTIONARY=$(location :rfc_dictionary)"],
    deps = [
        ":common_no_dictionary_data",
        "//java/org/brotli/wrapper/dec",
        "@junit_junit//jar",
    ],
)

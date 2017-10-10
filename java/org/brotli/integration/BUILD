# Description:
#   Integration test runner + corpus for Java port of Brotli decoder.

java_library(
    name = "brotli_jni_test_base",
    srcs = ["BrotliJniTestBase.java"],
    visibility = [
        "//java/org/brotli/wrapper/common:__pkg__",
        "//java/org/brotli/wrapper/dec:__pkg__",
        "//java/org/brotli/wrapper/enc:__pkg__",
    ],
)

java_library(
    name = "bundle_helper",
    srcs = ["BundleHelper.java"],
    visibility = [
        "//java/org/brotli/wrapper/dec:__pkg__",
        "//java/org/brotli/wrapper/enc:__pkg__",
    ],
)

java_library(
    name = "bundle_checker",
    srcs = ["BundleChecker.java"],
    deps = [
        ":bundle_helper",
        "//java/org/brotli/dec",
    ],
)

java_binary(
    name = "bundle_checker_bin",
    main_class = "org.brotli.integration.BundleChecker",
    runtime_deps = [":bundle_checker"],
)

java_test(
    name = "bundle_checker_data_test",
    args = ["java/org/brotli/integration/test_data.zip"],
    data = ["test_data.zip"],
    main_class = "org.brotli.integration.BundleChecker",
    use_testrunner = 0,
    runtime_deps = [":bundle_checker"],
)

java_test(
    name = "bundle_checker_fuzz_test",
    args = [
        "-s",
        "java/org/brotli/integration/fuzz_data.zip",
    ],
    data = ["fuzz_data.zip"],
    main_class = "org.brotli.integration.BundleChecker",
    use_testrunner = 0,
    runtime_deps = [":bundle_checker"],
)

filegroup(
    name = "test_data",
    srcs = ["test_data.zip"],
    visibility = [
        "//java/org/brotli/wrapper/dec:__pkg__",
    ],
)

filegroup(
    name = "test_corpus",
    srcs = ["test_corpus.zip"],
    visibility = [
        "//java/org/brotli/wrapper/enc:__pkg__",
    ],
)

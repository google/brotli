# Description:
#   Integration test runner + corpus for Java port of Brotli decoder.

java_library(
    name = "bundle_checker_lib",
    srcs = ["BundleChecker.java"],
    deps = ["//java/dec:lib"],
)

java_binary(
    name = "bundle_checker",
    main_class = "org.brotli.integration.BundleChecker",
    runtime_deps = [":bundle_checker_lib"],
)

java_test(
    name = "bundle_checker_test",
    args = ["java/integration/test_data.zip"],
    data = ["test_data.zip"],
    main_class = "org.brotli.integration.BundleChecker",
    use_testrunner = 0,
    runtime_deps = [":bundle_checker_lib"],
)

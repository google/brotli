load("@io_bazel_rules_closure//closure:defs.bzl", "closure_js_library", "closure_js_test")

package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # MIT

# Not a real polyfill. Do NOT use for anything, but tests.
closure_js_library(
    name = "polyfill",
    srcs = ["polyfill.js"],
    suppress = [
        "JSC_INVALID_OPERAND_TYPE",
        "JSC_MISSING_JSDOC",
        "JSC_STRICT_INEXISTENT_PROPERTY",
        "JSC_TYPE_MISMATCH",
        "JSC_UNKNOWN_EXPR_TYPE",
    ],
)

# Do NOT use this artifact; it is for test purposes only.
closure_js_library(
    name = "decode",
    srcs = ["decode.js"],
    suppress = ["JSC_USELESS_BLOCK"],
    deps = [":polyfill"],
)

closure_js_test(
    name = "all_tests",
    srcs = ["decode_test.js"],
    entry_points = ["decode_test.js"],
    suppress = ["moduleLoad"],
    deps = [
        ":decode",
        ":polyfill",
        "@io_bazel_rules_closure//closure/library:testing",
    ],
)

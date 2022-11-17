"""Utilities for Java brotli tests."""

_TEST_JVM_FLAGS = [
    "-DBROTLI_ENABLE_ASSERTS=true",
]

def brotli_java_test(name, main_class = None, jvm_flags = None, **kwargs):
    """test duplication rule that creates 32/64-bit test pair.

    Args:
       name: target name prefix
       main_class: override for test_class
       jvm_flags: base Java VM options
       **kwargs: pass-through
    """

    if jvm_flags == None:
        jvm_flags = []
    jvm_flags = jvm_flags + _TEST_JVM_FLAGS

    test_package = native.package_name().replace("/", ".").replace("third_party.brotli.java.", "")

    if main_class == None:
        test_class = test_package + "." + name
    else:
        test_class = None

    native.java_test(
        name = name + "_32",
        main_class = main_class,
        test_class = test_class,
        jvm_flags = jvm_flags + ["-DBROTLI_32_BIT_CPU=true"],
        visibility = ["//visibility:private"],
        **kwargs
    )

    native.java_test(
        name = name + "_64",
        main_class = main_class,
        test_class = test_class,
        jvm_flags = jvm_flags + ["-DBROTLI_32_BIT_CPU=false"],
        visibility = ["//visibility:private"],
        **kwargs
    )

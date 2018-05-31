package(
    default_visibility = ["//visibility:public"],
)

# >>> JNI headers

genrule(
    name = "copy_link_jni_header",
    srcs = ["@openjdk_linux//:jni_h"],
    outs = ["jni/jni.h"],
    cmd = "cp -f $< $@",
)

genrule(
    name = "copy_link_jni_md_header",
    srcs = select({
        "@org_brotli//:darwin": ["@openjdk_macos//:jni_md_h"],
        "@org_brotli//:darwin_x86_64": ["@openjdk_macos//:jni_md_h"],
        "@org_brotli//:windows_msys": ["@openjdk_win//:jni_md_h"],
        "@org_brotli//:windows_msvc": ["@openjdk_win//:jni_md_h"],
        "@org_brotli//:windows": ["@openjdk_win//:jni_md_h"],
        "//conditions:default": ["@openjdk_linux//:jni_md_h"],
    }),
    outs = ["jni/jni_md.h"],
    cmd = "cp -f $< $@",
)

cc_library(
    name = "jni_inc",
    hdrs = [
        ":jni/jni.h",
        ":jni/jni_md.h",
    ],
    includes = ["jni"],
)

# <<< JNI headers

########################################################
# WARNING: do not (transitively) depend on this target!
########################################################
cc_binary(
    name = "brotli_jni.dll",
    srcs = [
        "@org_brotli//:common_headers",
        "@org_brotli//:common_sources",
        "@org_brotli//:dec_headers",
        "@org_brotli//:dec_sources",
        "@org_brotli//:enc_headers",
        "@org_brotli//:enc_sources",
        "//org/brotli/wrapper/common:jni_src",
        "//org/brotli/wrapper/dec:jni_src",
        "//org/brotli/wrapper/enc:jni_src",
    ],
    deps = [
        "@org_brotli//:brotli_inc",
        ":jni_inc",
    ],
    linkshared = 1,
)

########################################################
# WARNING: do not (transitively) depend on this target!
########################################################
cc_binary(
    name = "brotli_jni_no_dictionary_data.dll",
    srcs = [
        "@org_brotli//:common_headers",
        "@org_brotli//:common_sources",
        "@org_brotli//:dec_headers",
        "@org_brotli//:dec_sources",
        "@org_brotli//:enc_headers",
        "@org_brotli//:enc_sources",
        "//org/brotli/wrapper/common:jni_src",
        "//org/brotli/wrapper/dec:jni_src",
        "//org/brotli/wrapper/enc:jni_src",
    ],
    defines = [
        "BROTLI_EXTERNAL_DICTIONARY_DATA=",
    ],
    deps = [
        "@org_brotli//:brotli_inc",
        ":jni_inc",
    ],
    linkshared = 1,
)

# Description:
#   Bazel workspace file for Brotli.

workspace(name = "org_brotli")

maven_jar(
    name = "junit_junit",
    artifact = "junit:junit:4.12",
)

git_repository(
    name = "io_bazel_rules_go",
    remote = "https://github.com/bazelbuild/rules_go.git",
    tag = "0.9.0",
)

http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "6691c58a2cd30a86776dd9bb34898b041e37136f2dc7e24cadaeaf599c95c657",
    strip_prefix = "rules_closure-08039ba8ca59f64248bb3b6ae016460fe9c9914f",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_closure/archive/08039ba8ca59f64248bb3b6ae016460fe9c9914f.tar.gz",
        "https://github.com/bazelbuild/rules_closure/archive/08039ba8ca59f64248bb3b6ae016460fe9c9914f.tar.gz",  # 2018-01-16
    ],
)

new_http_archive(
    name = "openjdk_linux",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-linux_x64.tar.gz",
    ],
    sha256 = "7e6284739c0e5b7142bc7a9adc61ced70dc5bb26b130b582b18e809013bcb251",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-linux_x64/include/jni.h"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-linux_x64/include/linux/jni_md.h"],
)""",
)

new_http_archive(
    name = "openjdk_macos",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-macosx_x64.zip",
    ],
    sha256 = "ff533364c9cbd3b271ab5328efe28e2dd6d7bae5b630098a5683f742ecf0709d",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-macosx_x64/include/darwin/jni_md.h"],
)""",
)

new_http_archive(
    name = "openjdk_win",
    urls = [
        "https://mirror.bazel.build/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
        "https://bazel-mirror.storage.googleapis.com/openjdk/azul-zulu-8.23.0.3-jdk8.0.144/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
        "https://cdn.azul.com/zulu/bin/zulu8.23.0.3-jdk8.0.144-win_x64.zip",
    ],
    sha256 = "f1d9d3341ef7c8c9baff3597953e99a6a7c64f8608ee62c03fdd7574b7655c02",
    build_file_content = """
package(
    default_visibility = ["//visibility:public"],
)
filegroup(
    name = "jni_md_h",
    srcs = ["zulu8.23.0.3-jdk8.0.144-win_x64/include/win32/jni_md.h"],
)""",
)

load("@io_bazel_rules_closure//closure:defs.bzl", "closure_repositories")
closure_repositories()

load("@io_bazel_rules_go//go:def.bzl", "go_rules_dependencies", "go_register_toolchains")
go_rules_dependencies()
go_register_toolchains()

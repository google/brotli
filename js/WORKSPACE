workspace(name = "org_brotli_js")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "io_bazel_rules_closure",
    sha256 = "d66deed38a0bb20581c15664f0ab62270af5940786855c7adc3087b27168b529",
    strip_prefix = "rules_closure-0.11.0",
    urls = [
        "https://mirror.bazel.build/github.com/bazelbuild/rules_closure/archive/0.11.0.tar.gz",
        "https://github.com/bazelbuild/rules_closure/archive/0.11.0.tar.gz",
    ],
)

load("@io_bazel_rules_closure//closure:repositories.bzl", "rules_closure_dependencies", "rules_closure_toolchains")
rules_closure_dependencies()
rules_closure_toolchains()

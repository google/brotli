# Description: brotli research tools.

package(default_visibility = ["//visibility:public"])

licenses(["notice"])  # MIT

cc_library(
    name = "dm",
    srcs = ["deorummolae.cc"],
    hdrs = [
        "deorummolae.h",
        "esaxx/sais.hxx",
    ],
)

cc_library(
    name = "sieve",
    srcs = ["sieve.cc"],
    hdrs = ["sieve.h"],
)

cc_binary(
    name = "dictionary_generator",
    srcs = ["dictionary_generator.cc"],
    deps = [
        ":dm",
        ":sieve",
    ],
)

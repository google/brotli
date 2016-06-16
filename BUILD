# Description:
#   Brotli is a generic-purpose lossless compression algorithm.

package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # MIT

STRICT_COMPILER_OPTIONS = [
    "--pedantic-errors",
    "-Wall",
    "-Wconversion",
    "-Werror",
    "-Wextra",
    "-Wlong-long",
    "-Wmissing-declarations",
    "-Wno-strict-aliasing",
    "-Wshadow",
    "-Wsign-compare",
]

STRICT_C_OPTIONS = STRICT_COMPILER_OPTIONS + [
    "-Wmissing-prototypes",
]

COMMON_HEADERS = [
    "common/constants.h",
    "common/dictionary.h",
    "common/port.h",
    "common/types.h",
]

COMMON_SOURCES = [
    "common/dictionary.c",
]

DEC_HEADERS = [
    "dec/bit_reader.h",
    "dec/context.h",
    "dec/decode.h",
    "dec/huffman.h",
    "dec/port.h",
    "dec/prefix.h",
    "dec/state.h",
    "dec/transform.h",
]

DEC_SOURCES = [
    "dec/bit_reader.c",
    "dec/decode.c",
    "dec/huffman.c",
    "dec/state.c",
]

ENC_HEADERS = [
    "enc/backward_references.h",
    "enc/backward_references_inc.h",
    "enc/bit_cost.h",
    "enc/bit_cost_inc.h",
    "enc/block_encoder_inc.h",
    "enc/block_splitter.h",
    "enc/block_splitter_inc.h",
    "enc/brotli_bit_stream.h",
    "enc/cluster.h",
    "enc/cluster_inc.h",
    "enc/command.h",
    "enc/compress_fragment.h",
    "enc/compress_fragment_two_pass.h",
    "enc/context.h",
    "enc/dictionary_hash.h",
    "enc/encode.h",
    "enc/entropy_encode.h",
    "enc/entropy_encode_static.h",
    "enc/fast_log.h",
    "enc/find_match_length.h",
    "enc/hash.h",
    "enc/hash_longest_match_inc.h",
    "enc/hash_longest_match_quickly_inc.h",
    "enc/histogram.h",
    "enc/histogram_inc.h",
    "enc/literal_cost.h",
    "enc/memory.h",
    "enc/metablock.h",
    "enc/metablock_inc.h",
    "enc/port.h",
    "enc/prefix.h",
    "enc/ringbuffer.h",
    "enc/static_dict.h",
    "enc/static_dict_lut.h",
    "enc/utf8_util.h",
    "enc/write_bits.h",
]

ENC_SOURCES = [
    "enc/backward_references.c",
    "enc/bit_cost.c",
    "enc/block_splitter.c",
    "enc/brotli_bit_stream.c",
    "enc/cluster.c",
    "enc/compress_fragment.c",
    "enc/compress_fragment_two_pass.c",
    "enc/encode.c",
    "enc/entropy_encode.c",
    "enc/histogram.c",
    "enc/literal_cost.c",
    "enc/memory.c",
    "enc/metablock.c",
    "enc/static_dict.c",
    "enc/utf8_util.c",
]

cc_library(
    name = "brotli_common",
    srcs = COMMON_SOURCES,
    hdrs = COMMON_HEADERS,
    copts = STRICT_C_OPTIONS,
)

cc_library(
    name = "brotli_dec",
    srcs = DEC_SOURCES,
    hdrs = DEC_HEADERS,
    copts = STRICT_C_OPTIONS,
    deps = [
        ":brotli_common",
    ],
)

cc_library(
    name = "brotli_enc",
    srcs = ENC_SOURCES,
    hdrs = ENC_HEADERS,
    copts = STRICT_C_OPTIONS,
    deps = [
        ":brotli_common",
    ],
)

cc_binary(
    name = "bro",
    srcs = ["tools/bro.cc"],
    copts = STRICT_COMPILER_OPTIONS,
    deps = [
        ":brotli_dec",
        ":brotli_enc",
    ],
)

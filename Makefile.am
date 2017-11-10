AUTOMAKE_OPTIONS = gnu nostdinc subdir-objects

ACLOCAL_AMFLAGS = -I m4

LIBBROTLI_VERSION_INFO = -version-info @PACKAGE_VERSION@

bin_PROGRAMS = brotli
lib_LTLIBRARIES = libbrotlicommon.la libbrotlidec.la libbrotlienc.la

# IT WOULD BE FOOLISH TO USE COMPUTERS TO AUTOMATE REPETITIVE TASKS:
# ENLIST EVERY USED HEADER AND SOURCE FILE MANUALLY!

BROTLI = \
  c/tools/brotli.c
COMMON = \
  c/common/dictionary.c
COMMONHEADERS = \
  c/common/constants.h \
  c/common/dictionary.h \
  c/common/version.h
DECODE = \
  c/dec/bit_reader.c \
  c/dec/decode.c \
  c/dec/huffman.c \
  c/dec/state.c
DECODEHEADERS = \
  c/dec/bit_reader.h \
  c/dec/context.h \
  c/dec/huffman.h \
  c/dec/port.h \
  c/dec/prefix.h \
  c/dec/state.h \
  c/dec/transform.h
ENCODE = \
  c/enc/backward_references.c \
  c/enc/backward_references_hq.c \
  c/enc/bit_cost.c \
  c/enc/block_splitter.c \
  c/enc/brotli_bit_stream.c \
  c/enc/cluster.c \
  c/enc/compress_fragment.c \
  c/enc/compress_fragment_two_pass.c \
  c/enc/dictionary_hash.c \
  c/enc/encode.c \
  c/enc/entropy_encode.c \
  c/enc/histogram.c \
  c/enc/literal_cost.c \
  c/enc/memory.c \
  c/enc/metablock.c \
  c/enc/static_dict.c \
  c/enc/utf8_util.c
ENCODEHEADERS = \
  c/enc/backward_references.h \
  c/enc/backward_references_hq.h \
  c/enc/backward_references_inc.h \
  c/enc/bit_cost.h \
  c/enc/bit_cost_inc.h \
  c/enc/block_encoder_inc.h \
  c/enc/block_splitter.h \
  c/enc/block_splitter_inc.h \
  c/enc/brotli_bit_stream.h \
  c/enc/cluster.h \
  c/enc/cluster_inc.h \
  c/enc/command.h \
  c/enc/compress_fragment.h \
  c/enc/compress_fragment_two_pass.h \
  c/enc/context.h \
  c/enc/dictionary_hash.h \
  c/enc/entropy_encode.h \
  c/enc/entropy_encode_static.h \
  c/enc/fast_log.h \
  c/enc/find_match_length.h \
  c/enc/hash_forgetful_chain_inc.h \
  c/enc/hash.h \
  c/enc/hash_longest_match64_inc.h \
  c/enc/hash_longest_match_inc.h \
  c/enc/hash_longest_match_quickly_inc.h \
  c/enc/hash_to_binary_tree_inc.h \
  c/enc/histogram.h \
  c/enc/histogram_inc.h \
  c/enc/literal_cost.h \
  c/enc/memory.h \
  c/enc/metablock.h \
  c/enc/metablock_inc.h \
  c/enc/port.h \
  c/enc/prefix.h \
  c/enc/quality.h \
  c/enc/ringbuffer.h \
  c/enc/static_dict.h \
  c/enc/static_dict_lut.h \
  c/enc/utf8_util.h \
  c/enc/write_bits.h

brotliincludedir = $(includedir)/brotli
brotliinclude_HEADERS = \
  c/include/brotli/decode.h \
  c/include/brotli/encode.h \
  c/include/brotli/port.h \
  c/include/brotli/types.h

AM_CFLAGS = -I$(top_srcdir)/c/include

brotli_SOURCES = $(BROTLI)

brotli_LDADD = libbrotlidec.la libbrotlienc.la libbrotlicommon.la -lm
libbrotlicommon_la_SOURCES = $(COMMON) $(COMMONHEADERS)
libbrotlicommon_la_LDFLAGS = $(AM_LDFLAGS) $(LIBBROTLI_VERSION_INFO) $(LDFLAGS)
libbrotlidec_la_SOURCES = $(DECODE) $(DECODEHEADERS)
libbrotlidec_la_LDFLAGS = $(AM_LDFLAGS) $(LIBBROTLI_VERSION_INFO) $(LDFLAGS)
libbrotlienc_la_SOURCES = $(ENCODE) $(ENCODEHEADERS)
libbrotlienc_la_LDFLAGS = $(AM_LDFLAGS) $(LIBBROTLI_VERSION_INFO) $(LDFLAGS)

#noinst_LIBRARIES = libbrotlicommon.a libbrotlidec.a libbrotlienc.a
#brotli_LDADD = libbrotlidec.a libbrotlienc.a libbrotlicommon.a -lm
#libbrotlicommon_a_SOURCES = $(COMMON) $(COMMONHEADERS)
#libbrotlicommon_a_CFLAGS = $(AM_CFLAGS)
#libbrotlidec_a_SOURCES = $(DECODE) $(DECODEHEADERS)
#libbrotlidec_a_CFLAGS = $(AM_CFLAGS)
#libbrotlienc_a_SOURCES = $(ENCODE) $(ENCODEHEADERS)
#libbrotlienc_a_CFLAGS = $(AM_CFLAGS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
  scripts/gnu/libbrotlicommon.pc \
  scripts/gnu/libbrotlidec.pc \
  scripts/gnu/libbrotlienc.pc
pkgincludedir= $(brotliincludedir)

dist_doc_DATA = README

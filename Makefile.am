AUTOMAKE_OPTIONS = foreign nostdinc subdir-objects

ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libbrotlidec.la libbrotlienc.la

DECODE = dec/bit_reader.c dec/decode.c dec/huffman.c dec/state.c	\
  dec/dictionary.c

ENCODE = enc/backward_references.cc enc/histogram.cc			\
 enc/block_splitter.cc enc/literal_cost.cc enc/brotli_bit_stream.cc	\
 enc/metablock.cc enc/encode.cc enc/static_dict.cc			\
 enc/encode_parallel.cc enc/streams.cc enc/entropy_encode.cc		\
 enc/dictionary.cc enc/utf8_util.cc enc/compress_fragment.cc		\
 enc/compress_fragment_two_pass.cc

DECODEHEADERS = dec/decode.h dec/state.h dec/types.h dec/bit_reader.h	\
 dec/huffman.h dec/port.h dec/context.h dec/dictionary.h		\
 dec/transform.h dec/prefix.h

ENCODEHEADERS = enc/encode.h enc/command.h enc/hash.h enc/ringbuffer.h	\
 enc/static_dict.h enc/streams.h enc/fast_log.h enc/prefix.h		\
 enc/dictionary_hash.h enc/find_match_length.h enc/port.h		\
 enc/transform.h enc/dictionary.h enc/backward_references.h		\
 enc/histogram.h enc/block_splitter.h enc/metablock.h enc/context.h	\
 enc/literal_cost.h enc/cluster.h enc/bit_cost.h enc/entropy_encode.h	\
 enc/brotli_bit_stream.h enc/write_bits.h enc/static_dict_lut.h		\
 enc/encode_parallel.h enc/types.h enc/utf8_util.h			\
 enc/compress_fragment.h enc/compress_fragment_two_pass.h		\
 enc/entropy_encode_static.h

EXTRA_DIST = CONTRIBUTING.md README.md

# install headers in $(includedir) with subdirs
nobase_include_HEADERS = $(DECODEHEADERS) $(ENCODEHEADERS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libbrotlienc.pc libbrotlidec.pc
pkgincludedir= $(includedir)/brotli
pkginclude_HEADERS = 

LIBBROTLIENC_VERSION_INFO = -version-info 1:0:0
LIBBROTLIDEC_VERSION_INFO = -version-info 1:0:0
# This flag accepts an argument of the form current[:revision[:age]]. So,
# passing -version-info 3:12:1 sets current to 3, revision to 12, and age to
# 1.
#
# If either revision or age are omitted, they default to 0. Also note that age
# must be less than or equal to the current interface number.
#
# Here are a set of rules to help you update your library version information: 
#
# 1.Start with version information of 0:0:0 for each libtool library.
#
# 2.Update the version information only immediately before a public release of
# your software. More frequent updates are unnecessary, and only guarantee
# that the current interface number gets larger faster.
#
# 3.If the library source code has changed at all since the last update, then
# increment revision (c:r+1:a)
#
# 4.If any interfaces have been added, removed, or changed since the last
# update, increment current, and set revision to 0. (c+1:r=0:a)
#
# 5.If any interfaces have been added since the last public release, then
# increment age. (c:r:a+1)
#
# 6.If any interfaces have been removed since the last public release, then
# set age to 0. (c:r:a=0)
#

libbrotlidec_la_CPPFLAGS_EXTRA = -DBROTLI_BUILDING_LIBRARY
libbrotlidec_la_LDFLAGS = $(AM_LDFLAGS) $(LIBBROTLIDEC_VERSION_INFO) $(LDFLAGS)
libbrotlidec_la_CFLAGS = $(AM_CFLAGS) $(libbrotli_la_CFLAGS_EXTRA)
libbrotlidec_la_CXXFLAGS =
libbrotlidec_la_CPPFLAGS = $(AM_CPPFLAGS) $(libbrotli_la_CPPFLAGS_EXTRA)
libbrotlidec_la_SOURCES = $(DECODE) $(DECODEHEADERS)

libbrotlienc_la_CPPFLAGS_EXTRA = -DBROTLI_BUILDING_LIBRARY
libbrotlienc_la_LDFLAGS = $(AM_LDFLAGS) $(LIBBROTLIENC_VERSION_INFO) $(LDFLAGS)
libbrotlienc_la_CFLAGS = $(AM_CFLAGS) $(libbrotli_la_CFLAGS_EXTRA)
libbrotlienc_la_CXXFLAGS =
libbrotlienc_la_CPPFLAGS = $(AM_CPPFLAGS) $(libbrotli_la_CPPFLAGS_EXTRA)
libbrotlienc_la_SOURCES = $(ENCODE) $(ENCODEHEADERS)

# where to install the brotli headers
libbrotlienc_ladir = $(includedir)
libbrotlidec_ladir = $(includedir)


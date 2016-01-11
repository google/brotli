#brotli/enc

include ../shared.mk

OBJS_NODICT = backward_references.o block_splitter.o brotli_bit_stream.o compress_fragment.o compress_fragment_two_pass.o encode.o encode_parallel.o entropy_encode.o histogram.o literal_cost.o metablock.o static_dict.o streams.o utf8_util.o
OBJS = $(OBJS_NODICT) dictionary.o

nodict : $(OBJS_NODICT)

all : $(OBJS)

clean :
	rm -f $(OBJS) $(SO)


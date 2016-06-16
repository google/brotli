#brotli/enc

include ../shared.mk

OBJS = backward_references.o bit_cost.o block_splitter.o brotli_bit_stream.o \
       cluster.o compress_fragment.o compress_fragment_two_pass.o compressor.o \
       encode.o encode_parallel.o entropy_encode.o histogram.o literal_cost.o \
       memory.o metablock.o static_dict.o streams.o utf8_util.o
all : $(OBJS)

clean :
	rm -f $(OBJS) $(SO)

.PHONY: all clean


#brotli/enc

include ../shared.mk

OBJS = backward_references.o block_splitter.o brotli_bit_stream.o encode.o encode_parallel.o entropy_encode.o histogram.o literal_cost.o metablock.o static_dict.o streams.o

all : $(OBJS)

clean :
	rm -f $(OBJS) $(SO)


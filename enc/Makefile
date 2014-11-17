#brotli/enc

include ../shared.mk

OBJS = backward_references.o block_splitter.o brotli_bit_stream.o encode.o entropy_encode.o histogram.o literal_cost.o

all : $(OBJS)

clean :
	rm -f $(OBJS) $(SO)


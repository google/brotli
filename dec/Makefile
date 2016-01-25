#brotli/dec

include ../shared.mk

CFLAGS += -Wall

OBJS = bit_reader.o decode.o dictionary.o huffman.o state.o

all : $(OBJS)

clean :
	rm -f $(OBJS)

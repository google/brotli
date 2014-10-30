#brotli/tools

include ../shared.mk

BROTLI = ..
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

EXECUTABLES=bro

EXE_OBJS=$(patsubst %, %.o, $(EXECUTABLES))

all : $(EXECUTABLES)

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CPP) $(LFLAGS) $(ENCOBJ) $(DECOBJ) $@.o -o $@

deps :
	make -C $(BROTLI)/dec
	make -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	make -C $(BROTLI)/dec clean
	make -C $(BROTLI)/enc clean

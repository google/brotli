#brotli/tools

include ../shared.mk

BROTLI = ..
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

EXECUTABLES=bro

EXE_OBJS=$(patsubst %, %.o, $(EXECUTABLES))

all : $(EXECUTABLES)

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LFLAGS) $(ENCOBJ) $(DECOBJ) $@.o -o $@

deps :
	$(MAKE) -C $(BROTLI)/dec
	$(MAKE) -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	$(MAKE) -C $(BROTLI)/dec clean
	$(MAKE) -C $(BROTLI)/enc clean

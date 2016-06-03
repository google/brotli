#brotli/tools

include ../shared.mk

BROTLI = ..
COMMONOBJ = $(BROTLI)/common/*.o
DECOBJ = $(BROTLI)/dec/*.o
ENCOBJ = $(BROTLI)/enc/*.o

EXECUTABLES=bro

EXE_OBJS=$(patsubst %, %.o, $(EXECUTABLES))

all : $(EXECUTABLES)

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LDFLAGS) $(COMMONOBJ) $(ENCOBJ) $(DECOBJ) $@.o -o $@

deps :
	$(MAKE) -C $(BROTLI)/common
	$(MAKE) -C $(BROTLI)/dec
	$(MAKE) -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	$(MAKE) -C $(BROTLI)/common clean
	$(MAKE) -C $(BROTLI)/dec clean
	$(MAKE) -C $(BROTLI)/enc clean

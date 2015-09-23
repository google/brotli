#brotli/tools

include ../shared.mk

BROTLI = ..
ENCOBJ = $(BROTLI)/enc/*.o
DECOBJ = $(BROTLI)/dec/*.o

EXECUTABLES=bro brot

EXE_OBJS=$(patsubst %, %.o, $(EXECUTABLES))

all : $(EXECUTABLES) links

tests: links brot
	./brot-test.sh

# Create hard-links for brot (similar to bzcat bunbzip2)
links: brot
	ln -f brot brotcat
	ln -f brot unbrot

$(EXECUTABLES) : $(EXE_OBJS) deps
	$(CXX) $(LFLAGS) $(ENCOBJ) $(DECOBJ) $@.o -o $@

deps :
	$(MAKE) -C $(BROTLI)/dec
	$(MAKE) -C $(BROTLI)/enc

clean :
	rm -f $(OBJS) $(EXE_OBJS) $(EXECUTABLES)
	$(MAKE) -C $(BROTLI)/dec clean
	$(MAKE) -C $(BROTLI)/enc clean

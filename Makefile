OS := $(shell uname)
LIBSOURCES = $(wildcard c/common/*.c) $(wildcard c/dec/*.c) \
             $(wildcard c/enc/*.c)
SOURCES = $(LIBSOURCES) c/tools/brotli.c
BINDIR = bin
OBJDIR = $(BINDIR)/obj
LIBOBJECTS = $(addprefix $(OBJDIR)/, $(LIBSOURCES:.c=.o))
OBJECTS = $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))
LIB_A = libbrotli.a
EXECUTABLE = brotli
DIRS = $(OBJDIR)/c/common $(OBJDIR)/c/dec $(OBJDIR)/c/enc \
       $(OBJDIR)/c/tools $(BINDIR)/tmp
CFLAGS += -O2
ifeq ($(os), Darwin)
  CPPFLAGS += -DOS_MACOSX
endif

all: test
	@:

.PHONY: all clean test

$(DIRS):
	mkdir -p $@

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -lm -o $(BINDIR)/$(EXECUTABLE)

lib: $(LIBOBJECTS)
	rm -f $(LIB_A)
	ar -crs $(LIB_A) $(LIBOBJECTS)

test: $(EXECUTABLE)
	tests/compatibility_test.sh
	tests/roundtrip_test.sh

clean:
	rm -rf $(BINDIR) $(LIB_A)

.SECONDEXPANSION:
$(OBJECTS): $$(patsubst %.o,%.c,$$(patsubst $$(OBJDIR)/%,%,$$@)) | $(DIRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -Ic/include \
        -c $(patsubst %.o,%.c,$(patsubst $(OBJDIR)/%,%,$@)) -o $@

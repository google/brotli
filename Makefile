OS := $(shell uname)
LIBSOURCES = $(wildcard common/*.c) $(wildcard dec/*.c) $(wildcard enc/*.c)
SOURCES = $(LIBSOURCES) tools/bro.c
BINDIR = bin
OBJDIR = $(BINDIR)/obj
LIBOBJECTS = $(addprefix $(OBJDIR)/, $(LIBSOURCES:.c=.o))
OBJECTS = $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))
LIB_A = libbrotli.a
EXECUTABLE = bro
DIRS = $(OBJDIR)/common $(OBJDIR)/dec $(OBJDIR)/enc \
       $(OBJDIR)/tools $(BINDIR)/tmp
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
	$(CC) $(CFLAGS) $(CPPFLAGS) -Iinclude \
        -c $(patsubst %.o,%.c,$(patsubst $(OBJDIR)/%,%,$@)) -o $@

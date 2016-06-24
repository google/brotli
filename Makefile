OS := $(shell uname)
SOURCES = $(wildcard common/*.c) $(wildcard dec/*.c) $(wildcard enc/*.c) \
          tools/bro.c
BINDIR = bin
OBJDIR = $(BINDIR)/obj
OBJECTS = $(addprefix $(OBJDIR)/, $(SOURCES:.c=.o))
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

$(OBJECTS): $(DIRS)
	$(CC) $(CFLAGS) $(CPPFLAGS) \
        -c $(patsubst %.o,%.c,$(patsubst $(OBJDIR)/%,%,$@)) -o $@

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -lm -o $(BINDIR)/$(EXECUTABLE)

test: $(EXECUTABLE)
	tests/compatibility_test.sh
	tests/roundtrip_test.sh

clean:
	rm -rf $(BINDIR)

OS := $(shell uname)

GFLAGS=-no-canonical-prefixes -fno-omit-frame-pointer -m64

CPP = g++
LFLAGS =
CPPFLAGS = -c -std=c++0x $(GFLAGS)

EMCC = emcc
EMCCFLAGS = -O1 -W -Wall

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  CPPFLAGS += -fno-tree-vrp
endif

%.o : %.c
	$(CPP) $(CPPFLAGS) $< -o $@

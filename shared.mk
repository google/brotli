OS := $(shell uname)

CC ?= gcc
CXX ?= g++

EMCC = emcc
EMCCFLAGS = -O1 -W -Wall

COMMON_FLAGS = -fno-omit-frame-pointer -no-canonical-prefixes -O2

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
else
  COMMON_FLAGS += -fno-tree-vrp
endif

CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS) -std=c++11

OS := $(shell uname)

CC ?= gcc
CXX ?= g++

COMMON_FLAGS = -fno-omit-frame-pointer -no-canonical-prefixes -O2

ifeq ($(OS), Darwin)
  CPPFLAGS += -DOS_MACOSX
endif

CFLAGS += $(COMMON_FLAGS)
CXXFLAGS += $(COMMON_FLAGS)

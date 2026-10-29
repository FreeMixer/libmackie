# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# libmackie: the Mackie Control (MCU) wire protocol as libmackie.a. See BUILDING.md.
#
# `make shared` and `make install-shared` are what the packages build: the same objects as a
# shared library.

CC      ?= cc
AR      ?= ar
CFLAGS  ?= -O2 -g
LDFLAGS ?=
# -fPIC always: the same objects go into the archive and the shared library.
WARN    := -std=c11 -Wall -Wextra -Werror -fPIC
INC     := -Iinclude
PREFIX  ?= /usr/local
LIBDIR  ?= $(PREFIX)/lib
INCDIR  ?= $(PREFIX)/include
PCDIR   ?= $(LIBDIR)/pkgconfig
DESTDIR ?=

# The version is the VERSION file; the soname's major is MACKIE_ABI in mackie.h. The header's
# version string and the packaging copies are held to them by tests/conformance-packaging.sh.
VERSION := $(shell cat VERSION)
ABI     := $(shell sed -n 's/^\#define MACKIE_ABI  *\([0-9]*\)$$/\1/p' include/mackie/mackie.h)

OBJS := src/mackie_map.o

TESTS :=

RUN_TEST := tests/run-test.sh

all: libmackie.a

# -MMD -MP: a header edit rebuilds every object that includes it.
src/%.o: src/%.c
	$(CC) $(CFLAGS) $(WARN) -MMD -MP $(INC) -c $< -o $@

-include $(OBJS:.o=.d)

libmackie.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

# ---- shared library (the packages) ---------------------------------------------------------
# --no-undefined: a symbol nothing defines fails here, not at a consumer's link.
SO := libmackie.so.$(VERSION)

$(SO): $(OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libmackie.so.$(ABI) -Wl,--no-undefined -o $@ $(OBJS)
	ln -sf $@ libmackie.so.$(ABI)
	ln -sf libmackie.so.$(ABI) libmackie.so

shared: $(SO)


clean:
	rm -f $(OBJS) $(OBJS:.o=.d) libmackie.a libmackie.so* *.pc $(TESTS)
	rm -rf build

.PHONY: all test shared clean

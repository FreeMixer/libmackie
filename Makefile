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

OBJS := src/mackie_map.o src/mackie_text.o src/mackie_codec.o src/mackie_display.o src/mackie_sysex.o

TESTS := tests/test_mcu tests/test_golden

RUN_TEST := tests/run-test.sh

all: libmackie.a

# -MMD -MP: a header edit rebuilds every object that includes it.
src/%.o: src/%.c
	$(CC) $(CFLAGS) $(WARN) -MMD -MP $(INC) -c $< -o $@

-include $(OBJS:.o=.d)

libmackie.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)

tests/test_mcu: tests/test_mcu.c tests/check.h tests/golden/mcu_golden.h libmackie.a
	$(CC) $(CFLAGS) $(WARN) $(INC) $< libmackie.a -o $@

tests/test_golden: tests/test_golden.c tests/check.h tests/golden/mcu_golden.h libmackie.a
	$(CC) $(CFLAGS) $(WARN) $(INC) $< libmackie.a -o $@

# ---- shared library (the packages) ---------------------------------------------------------
# --no-undefined: a symbol nothing defines fails here, not at a consumer's link.
SO := libmackie.so.$(VERSION)

$(SO): $(OBJS)
	$(CC) $(LDFLAGS) -shared -Wl,-soname,libmackie.so.$(ABI) -Wl,--no-undefined -o $@ $(OBJS)
	ln -sf $@ libmackie.so.$(ABI)
	ln -sf libmackie.so.$(ABI) libmackie.so

shared: $(SO)

test: $(TESTS)
	$(RUN_TEST) tests/test_mcu
	$(RUN_TEST) tests/test_golden
	$(RUN_TEST) tests/test_install.sh
	$(RUN_TEST) tests/conformance-packaging.sh

# The pkg-config file is written at install time, from the install's own PREFIX and LIBDIR: a
# file kept from an earlier build would name that build's directories.
PCSED = sed -e 's|@PREFIX@|$(PREFIX)|' -e 's|@LIBDIR@|$(LIBDIR)|' -e 's|@INCDIR@|$(INCDIR)|' \
	    -e 's|@VERSION@|$(VERSION)|'

# What both installs carry: headers and the pkg-config file.
install-common:
	install -d $(DESTDIR)$(INCDIR)/mackie $(DESTDIR)$(PCDIR)
	install -m 644 include/mackie/*.h $(DESTDIR)$(INCDIR)/mackie/
	$(PCSED) packaging/libmackie.pc.in > $(DESTDIR)$(PCDIR)/libmackie.pc

install: libmackie.a install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 644 libmackie.a $(DESTDIR)$(LIBDIR)/

# The packages: the shared library with its soname links.
install-shared: shared install-common
	install -d $(DESTDIR)$(LIBDIR)
	install -m 755 $(SO) $(DESTDIR)$(LIBDIR)/
	ln -sf $(SO) $(DESTDIR)$(LIBDIR)/libmackie.so.$(ABI)
	ln -sf libmackie.so.$(ABI) $(DESTDIR)$(LIBDIR)/libmackie.so

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) libmackie.a libmackie.so* *.pc $(TESTS)
	rm -rf build

.PHONY: all test shared install install-common install-shared clean

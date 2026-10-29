#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# `make install` into a scratch root, then build and run a consumer against that root through
# pkg-config alone: every installed header, archive and .pc file has to be there.
set -u
command -v pkg-config >/dev/null 2>&1 || { echo "no pkg-config: nothing tested"; exit 77; }
top=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT
make -s -C "$top" install PREFIX=/usr DESTDIR="$tmp/root" >/dev/null || { echo "FAIL: make install"; exit 1; }
cat > "$tmp/consumer.c" <<'C'
#include <mackie/mackie.h>
#include <mackie/mackie_codec.h>
int main(void)
{
	struct mackie_msg m;
	if (mackie_device_query_encode(MACKIE_MODEL_MCU, &m) || m.len != 7)
		return 1;
	return m.bytes[0] == 0xf0 && m.bytes[4] == 0x14 && m.bytes[6] == 0xf7 ? 0 : 1;
}
C
PKG_CONFIG_SYSROOT_DIR="$tmp/root" PKG_CONFIG_LIBDIR="$tmp/root/usr/lib/pkgconfig" \
	sh -c "${CC:-cc} '$tmp/consumer.c' \$(pkg-config --cflags --libs libmackie) -o '$tmp/consumer'" ||
	{ echo "FAIL: a consumer does not build against the installed tree"; exit 1; }
"$tmp/consumer" || { echo "FAIL: the consumer gave the wrong answer"; exit 1; }

# A second install to another prefix names that prefix: nothing from the first one is reused.
make -s -C "$top" install PREFIX=/opt/elsewhere DESTDIR="$tmp/other" >/dev/null || { echo "FAIL: second make install"; exit 1; }
grep -qx 'includedir=/opt/elsewhere/include' "$tmp/other/opt/elsewhere/lib/pkgconfig/libmackie.pc" ||
	{ echo "FAIL: libmackie.pc of the second install names another prefix"; exit 1; }
echo "test_install: ok"

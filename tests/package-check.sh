#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# The packages, installed in a clean Fedora or Debian container that holds nothing else:
#
#   tests/package-check.sh <directory holding the .rpm or .deb files>
#
# installs libmackie and its development package with what they pull in, then builds a consumer
# outside the tree against the development package through pkg-config alone, and runs it against
# the installed shared library.
set -eu
dir=$(cd "$1" && pwd)
if command -v dnf >/dev/null 2>&1; then
	dnf install -y -q --setopt=install_weak_deps=False gcc pkgconf-pkg-config \
		$(ls "$dir"/libmackie-[0-9]*.rpm "$dir"/libmackie-devel-[0-9]*.rpm | grep -v -e debuginfo -e debugsource -e '\.src\.rpm')
	rpm -q libmackie libmackie-devel
else
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -qq
	apt-get install -y -qq --no-install-recommends gcc libc6-dev pkgconf \
		"$dir"/libmackie0_*.deb "$dir"/libmackie-dev_*.deb
	dpkg -l libmackie0 libmackie-dev | tail -n 2
fi

tmp=$(mktemp -d)
cat > "$tmp/consumer.c" <<'C'
#include <stdio.h>
#include <mackie/mackie.h>
#include <mackie/mackie_codec.h>
int main(void)
{
	struct mackie_msg m;
	if (mackie_fader_encode(0, 1.0, &m) || m.len != 3)
		return 1;
	printf("consumer: libmackie %s, fader 1 at full scale = %02x %02x %02x\n",
	       MACKIE_VERSION_STRING, m.bytes[0], m.bytes[1], m.bytes[2]);
	return m.bytes[0] == 0xe0 && m.bytes[1] == 0x7f && m.bytes[2] == 0x7f ? 0 : 1;
}
C
cc "$tmp/consumer.c" $(pkg-config --cflags --libs libmackie) -o "$tmp/consumer"
"$tmp/consumer"
# linked against the shared object, not a copy
ldd "$tmp/consumer" | grep -q 'libmackie\.so\.0 => /' || { echo "FAIL: not linked to libmackie.so.0"; exit 1; }
pkg-config --modversion libmackie
echo "package-check: ok"

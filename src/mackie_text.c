/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 */
#include "mackie_text.h"

#define REPLACEMENT 0xfffdu

uint32_t mackie_utf8_next(const char **p)
{
	const uint8_t *s = (const uint8_t *)*p;
	uint32_t c = s[0], min;
	unsigned more;

	if (c == 0)
		return 0;
	if (c < 0x80) {
		*p += 1;
		return c;
	}
	if ((c & 0xe0) == 0xc0) {
		c &= 0x1f; more = 1; min = 0x80;
	} else if ((c & 0xf0) == 0xe0) {
		c &= 0x0f; more = 2; min = 0x800;
	} else if ((c & 0xf8) == 0xf0) {
		c &= 0x07; more = 3; min = 0x10000;
	} else {
		*p += 1;
		return REPLACEMENT;
	}
	for (unsigned i = 1; i <= more; i++) {
		if ((s[i] & 0xc0) != 0x80) {
			*p += 1;
			return REPLACEMENT;
		}
		c = (c << 6) | (s[i] & 0x3f);
	}
	if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
		*p += 1;
		return REPLACEMENT;
	}
	*p += 1 + more;
	return c;
}

size_t mackie_utf16_head(const char *text, uint16_t *u, size_t max)
{
	size_t n = 0;
	uint32_t c;

	while (n < max && (c = mackie_utf8_next(&text)) != 0) {
		if (c < 0x10000) {
			u[n++] = (uint16_t)c;
			continue;
		}
		c -= 0x10000;
		u[n++] = (uint16_t)(0xd800 | (c >> 10));
		if (n < max)
			u[n++] = (uint16_t)(0xdc00 | (c & 0x3ff));
	}
	return n;
}

size_t mackie_utf16_chars(const uint16_t *u, size_t n, uint32_t *cp)
{
	size_t k = 0;

	for (size_t i = 0; i < n; i++) {
		uint32_t c = u[i];
		if (c >= 0xd800 && c <= 0xdbff && i + 1 < n && u[i + 1] >= 0xdc00 && u[i + 1] <= 0xdfff) {
			c = 0x10000 + ((c - 0xd800) << 10) + (u[i + 1] - 0xdc00u);
			i++;
		}
		cp[k++] = c;
	}
	return k;
}

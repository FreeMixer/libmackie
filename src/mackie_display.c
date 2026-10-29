/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * The seven-segment displays: the 2-character assignment display and the timecode display,
 * one CC per digit.
 */
#include <mackie/mackie_codec.h>
#include "mackie_text.h"

/* Characters outside ASCII whose upper case begins with a letter the display draws. The
 * TypeScript folded case with String.prototype.toUpperCase() and took the first UTF-16 unit
 * of the result; these are every code point (U+0080..U+10FFFF) for which that unit falls in
 * 0x20..0x5F, as Node's Unicode tables give them. tools/gen-goldens.mjs derives the list
 * afresh and the golden test holds this table to it. */
static const struct { uint16_t cp; uint8_t upper; } FOLDS[] = {
	{ 0x00df, 'S' }, /* sharp s -> "SS" */
	{ 0x0131, 'I' }, /* dotless i */
	{ 0x017f, 'S' }, /* long s */
	{ 0x01f0, 'J' }, /* j with caron -> J + combining caron */
	{ 0x1e96, 'H' }, { 0x1e97, 'T' }, { 0x1e98, 'W' }, { 0x1e99, 'Y' }, { 0x1e9a, 'A' },
	{ 0xfb00, 'F' }, { 0xfb01, 'F' }, { 0xfb02, 'F' }, { 0xfb03, 'F' }, { 0xfb04, 'F' },
	{ 0xfb05, 'S' }, { 0xfb06, 'S' }, /* the ff, fi, fl, ffi, ffl, long-st and st ligatures */
};

int mackie_seven_segment_code(uint32_t cp)
{
	uint32_t c = cp;

	if (c >= 'a' && c <= 'z')
		c -= 0x20;
	else if (c >= 0x80) {
		c = 0;
		for (size_t i = 0; i < sizeof FOLDS / sizeof FOLDS[0]; i++)
			if (FOLDS[i].cp == cp)
				c = FOLDS[i].upper;
		if (!c)
			return 0x20;
	}
	if (c >= 0x40 && c <= 0x5f)
		return (int)(c - 0x40);
	if (c >= 0x20 && c <= 0x3f)
		return (int)c;
	return 0x20;
}

int mackie_timecode_encode(const struct mackie_map *m, const char *text, struct mackie_msg *out,
			   size_t cap, size_t *n)
{
	uint16_t u[MACKIE_DISPLAY_MAX_DIGITS];
	uint32_t cp[MACKIE_DISPLAY_MAX_DIGITS];

	if (!m || !text || !n || (cap && !out))
		return MACKIE_E_ARG;
	unsigned digits = m->timecode_digits;
	if (digits < 1 || digits > MACKIE_DISPLAY_MAX_DIGITS)
		return MACKIE_E_RANGE;
	/* cut to the digits, then right-align with blanks */
	size_t got = mackie_utf16_head(text, u, digits), pad = digits - got;
	for (size_t i = got; i-- > 0;)
		u[i + pad] = u[i];
	for (size_t i = 0; i < pad; i++)
		u[i] = ' ';
	size_t k = mackie_utf16_chars(u, digits, cp);
	if (k > cap)
		return MACKIE_E_SPACE;
	for (size_t i = 0; i < k; i++) {
		out[i].bytes[0] = MACKIE_CONTROL_CHANGE;
		out[i].bytes[1] = (uint8_t)(m->timecode_cc + digits - 1 - i);
		out[i].bytes[2] = (uint8_t)mackie_seven_segment_code(cp[i]);
		out[i].len = 3;
	}
	*n = k;
	return MACKIE_OK;
}

int mackie_assignment_encode(const struct mackie_map *m, const char *text,
			     struct mackie_msg out[2])
{
	uint16_t u[2] = { ' ', ' ' };

	if (!m || !text || !out)
		return MACKIE_E_ARG;
	/* each digit is one UTF-16 unit of the text padded to two: half a surrogate pair is a
	 * blank */
	mackie_utf16_head(text, u, 2);
	for (unsigned i = 0; i < 2; i++) {
		out[i].bytes[0] = MACKIE_CONTROL_CHANGE;
		out[i].bytes[1] = (uint8_t)(m->assignment_cc + 1 - i);
		out[i].bytes[2] = (uint8_t)mackie_seven_segment_code(u[i]);
		out[i].len = 3;
	}
	return MACKIE_OK;
}

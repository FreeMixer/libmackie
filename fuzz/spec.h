// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The protocol as the harnesses hold it, written from the Mackie Control wire format and the
 * contracts in include/mackie/mackie.h and mackie_codec.h, and from the bytes captured live
 * from an X-Touch (the MACKIE_HS_* exchange). No check compares libmackie with itself: a
 * check that takes its expected value from the code it judges holds for a library that is
 * wrong the same way. Every value here stands on a `contract:` line at the check that uses
 * it; the few values only the library's tables hold (the seven-segment folds) say so.
 *
 * The harnesses' own readers: spec_utf8 (UTF-8 by RFC 3629's table of well-formed byte
 * ranges), spec_units and spec_chars (UTF-16 as a string slice and a string iterator read
 * it), spec_seg (the seven-segment code of a character), spec_owner (the control a note
 * names on a map whose notes all differ), spec_scaled (a rounding done in integers). */
#ifndef MACKIE_FUZZ_SPEC_H
#define MACKIE_FUZZ_SPEC_H

#include <stddef.h>
#include <stdint.h>

/* return codes, mackie.h enum mackie_status */
#define SPEC_OK         0
#define SPEC_E_ARG      (-1)
#define SPEC_E_SPACE    (-2)
#define SPEC_E_RANGE    (-3)
#define SPEC_E_NOT_MINE (-4)

/* MIDI status nibbles and SysEx framing */
#define SPEC_NOTE_OFF   0x80
#define SPEC_NOTE_ON    0x90
#define SPEC_CC         0xb0
#define SPEC_PRESSURE   0xd0
#define SPEC_BEND       0xe0
#define SPEC_SYSEX      0xf0
#define SPEC_EOX        0xf7

/* F0 00 00 66 <model> <sub> */
#define SPEC_SUB_LCD     0x12
#define SPEC_SUB_RESET   0x08
#define SPEC_SUB_COLOUR  0x72
#define SPEC_SUB_QUERY   0x00
#define SPEC_SUB_REPLY   0x02
#define SPEC_SUB_VERSION_REQUEST 0x13
#define SPEC_SUB_VERSION_REPLY   0x14

#define SPEC_BEND_MAX   16383
#define SPEC_LCD_ROW    56
#define SPEC_LCD_CELLS  112
#define SPEC_CELL       7
#define SPEC_SERIAL     7
#define SPEC_CHALLENGE  4
#define SPEC_METER_MAX  12
#define SPEC_RING_MAX   11
#define SPEC_STRIPS     8
#define SPEC_COLOURS    8

/* the buttons of mackie_codec.h's enum mackie_button_kind, in its order */
#define SPEC_BTN_STRIP0 1   /* REC, SOLO, MUTE, SELECT, VPOT_PUSH: 1 + the button */
#define SPEC_BTN_TOUCH  6
#define SPEC_BTN_GLOBAL 7   /* BANK_LEFT ...: 7 + the global */
#define SPEC_BTN_KEY    23  /* 7 + the 16 globals */

/* a number the encoders round, as fuzz_num() makes it */
#define SPEC_NUM_NAN    0
#define SPEC_NUM_POS    1
#define SPEC_NUM_NEG    2
#define SPEC_NUM_FINITE 3

/* `value` clamped into [0, hi] and rounded, a half up, times `scale` for the fader: the
 * number is n / 2^20, so n * scale + 2^19 shifted down by 20 is floor(x * scale + 1/2) with
 * no floating point in it. NaN reads as 0 (mackie_codec.h: "NaN reads as 0"). */
static inline unsigned spec_scaled(int kind, int64_t n, unsigned hi, unsigned scale)
{
	if (kind == SPEC_NUM_NAN || kind == SPEC_NUM_NEG)
		return 0;
	if (kind == SPEC_NUM_POS || n >= ((int64_t)hi << 20))
		return hi * scale;
	if (n <= 0)
		return 0;
	return (unsigned)(((uint64_t)n * scale + (1u << 19)) >> 20);
}

/* The next character of NUL-terminated UTF-8, advancing *pp: the well-formed sequences of
 * RFC 3629 (table of byte ranges: C2..DF 80..BF; E0 A0..BF 80..BF; E1..EC and EE..EF 80..BF
 * 80..BF; ED 80..9F 80..BF; F0 90..BF 80..BF 80..BF; F1..F3 80..BF x3; F4 80..8F 80..BF
 * 80..BF), anything else U+FFFD and one byte consumed. 0 at the end.
 * contract: include/mackie/mackie_codec.h, the head comment; src/mackie_text.h */
static inline uint32_t spec_utf8(const unsigned char **pp)
{
	const unsigned char *s = *pp;
	unsigned c = s[0], lo = 0x80, hi = 0xbf, more;
	uint32_t cp;

	if (c == 0)
		return 0;
	if (c < 0x80) {
		*pp += 1;
		return c;
	}
	if (c >= 0xc2 && c <= 0xdf) {
		more = 1; cp = c & 0x1f;
	} else if (c >= 0xe0 && c <= 0xef) {
		more = 2; cp = c & 0x0f;
		if (c == 0xe0) lo = 0xa0;
		if (c == 0xed) hi = 0x9f;
	} else if (c >= 0xf0 && c <= 0xf4) {
		more = 3; cp = c & 0x07;
		if (c == 0xf0) lo = 0x90;
		if (c == 0xf4) hi = 0x8f;
	} else {
		*pp += 1;
		return 0xfffd;
	}
	for (unsigned i = 1; i <= more; i++) {
		if (s[i] < lo || s[i] > hi) {
			*pp += 1;
			return 0xfffd;
		}
		lo = 0x80; hi = 0xbf;
		cp = cp << 6 | (s[i] & 0x3f);
	}
	*pp += more + 1;
	return cp;
}

/* The first `max` UTF-16 units of the text, as a slice of the string's units: a character
 * past the BMP that does not fit whole leaves its high surrogate alone. */
static inline size_t spec_units(const char *text, uint16_t *u, size_t max)
{
	const unsigned char *p = (const unsigned char *)text;
	size_t n = 0;
	uint32_t c;

	while ((c = spec_utf8(&p)) != 0) {
		if (c < 0x10000) {
			if (n == max)
				break;
			u[n++] = (uint16_t)c;
		} else {
			if (n == max)
				break;
			u[n++] = (uint16_t)(0xd800 + ((c - 0x10000) >> 10));
			if (n == max)
				break;
			u[n++] = (uint16_t)(0xdc00 + ((c - 0x10000) & 0x3ff));
		}
	}
	return n;
}

/* Units back to characters, as a string iterator yields them: a high surrogate followed by
 * a low one is one character, any other unit is itself. */
static inline size_t spec_chars(const uint16_t *u, size_t n, uint32_t *cp)
{
	size_t k = 0, i = 0;

	while (i < n) {
		if (u[i] >= 0xd800 && u[i] < 0xdc00 && i + 1 < n && u[i + 1] >= 0xdc00 && u[i + 1] < 0xe000) {
			cp[k++] = 0x10000 + ((uint32_t)(u[i] - 0xd800) << 10) + (u[i + 1] - 0xdc00);
			i += 2;
		} else
			cp[k++] = u[i++];
	}
	return k;
}

/* The MCU seven-segment code of a character: '@' and A-Z and '[' '\' ']' '^' '_' are 0x00..0x1F
 * (the code is the character less 0x40), space through '?' are themselves, lower case folds
 * to upper, and anything else is a blank (0x20). A few characters above ASCII fold to a
 * letter because their upper case begins with one ('ß' is "SS"); those are the TypeScript's
 * toUpperCase() tables, which only tools/gen-goldens.mjs and src/mackie_display.c hold.
 * contract: include/mackie/mackie_codec.h mackie_seven_segment_code */
static inline int spec_seg(uint32_t c)
{
	static const struct { uint32_t cp; char up; } fold[] = {
		{ 0xdf, 'S' }, { 0x131, 'I' }, { 0x17f, 'S' }, { 0x1f0, 'J' }, { 0x1e96, 'H' },
		{ 0x1e97, 'T' }, { 0x1e98, 'W' }, { 0x1e99, 'Y' }, { 0x1e9a, 'A' }, { 0xfb00, 'F' },
		{ 0xfb01, 'F' }, { 0xfb02, 'F' }, { 0xfb03, 'F' }, { 0xfb04, 'F' }, { 0xfb05, 'S' },
		{ 0xfb06, 'S' },
	};
	uint32_t u = c;

	if (c >= 0x80) {
		u = 0;
		for (unsigned i = 0; i < sizeof fold / sizeof fold[0]; i++)
			if (fold[i].cp == c)
				u = (uint32_t)fold[i].up;
	} else if (c >= 'a' && c <= 'z')
		u = c - 'a' + 'A';
	if (u >= 0x40 && u <= 0x5f)
		return (int)(u - 0x40);
	if (u >= 0x20 && u <= 0x3f)
		return (int)u;
	return 0x20;
}

/* What a note names on a map whose notes all differ. */
struct spec_owner {
	int kind;       /* SPEC_BTN_* */
	int strip;      /* 0..8, or -1 */
	const char *id; /* for a key */
};

#endif /* MACKIE_FUZZ_SPEC_H */

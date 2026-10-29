/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * Internal: text as the surface cells read it. A cell holds one character, so UTF-8 is
 * read as code points; widths are counted in UTF-16 code units, as the TypeScript this was
 * ported from counted them (String.prototype.slice, padStart, padEnd).
 */
#ifndef MACKIE_TEXT_H
#define MACKIE_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* The next code point of NUL-terminated UTF-8 at *p, advancing *p; 0 at the end. An invalid
 * or overlong sequence, a surrogate or a value past U+10FFFF reads as U+FFFD and consumes
 * one byte. */
uint32_t mackie_utf8_next(const char **p);

/* The first max UTF-16 code units of text into u[]; returns how many. A character outside
 * the BMP that does not fit whole leaves its high surrogate alone, as a slice does. */
size_t mackie_utf16_head(const char *text, uint16_t *u, size_t max);

/* UTF-16 units back to the characters a string iterator yields: a surrogate pair is one
 * character, a lone surrogate is itself. Returns how many went into cp[] (at most n). */
size_t mackie_utf16_chars(const uint16_t *u, size_t n, uint32_t *cp);

#endif

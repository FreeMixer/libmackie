// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* Writes the seed corpus of every harness in fuzz/ (`make fuzz-corpus`).
 *
 * Each seed is shaped the way its harness reads its input; the harness's header comment is the
 * grammar. Beside well-formed messages it writes one input per refusal a reader makes and one
 * per edge a check holds: a Note Off, a note the map does not name, a missing velocity, a
 * data byte with the high bit set, an overlong or astral character, a scribble past the last
 * cell, a map with one broken field, and so on. Every name here is the `input:` of a record in
 * a harness, so the seeds are what the records and the sabotage patches are proved on. Nothing
 * is fetched and nothing is random, so the corpus regenerates byte for byte on any machine.
 *
 * Usage: fuzz-gen-corpus <corpus dir>. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#include "fuzz.h"

static uint8_t buf[4096];
static size_t bn;
static const char *dir, *harness;

static void put8(unsigned v) { buf[bn++] = (uint8_t)v; }
static void put16(unsigned v) { put8(v & 0xff); put8(v >> 8 & 0xff); }
static void put32(uint32_t v) { put16(v & 0xffff); put16(v >> 16); }
static void putn(const void *p, size_t n) { memcpy(buf + bn, p, n); bn += n; }
static void putlist(size_t n, const uint8_t *p) { put8((unsigned)n); putn(p, n); }
static void puttext(const char *t) { put16((unsigned)strlen(t)); putn(t, strlen(t)); }
static void putfin(int32_t n) { put8(5); put32((uint32_t)n); }

#define B(...) do { static const uint8_t b_[] = { __VA_ARGS__ }; putlist(sizeof b_, b_); } while (0)

/* The bytes fuzz_map_init reads: notes from nb in steps of ns | 1, CCs from cb in steps of
 * cs | 1, the jog byte (high bit: no jog), the fader channels, the assignment CC, digits - 1,
 * the timecode CC and the key count. The default map: notes 0x10 up, V-Pot CCs 0x20, rings
 * 0x28, jog CC 0x30; REC 0x10.., SOLO 0x18.., MUTE 0x20.., SELECT 0x28.., V-Pot push 0x30..,
 * touch 0x38..0x40, globals 0x41..0x50, keys 0x51 and 0x52. */
static void map_bytes(unsigned jog, unsigned faders, unsigned assign, unsigned digits_m1,
		      unsigned tc, unsigned nkeys)
{
	put8(0x10); put8(0); put8(0x20); put8(0);
	put8(jog);
	put16(faders);
	put8(assign);
	put8(digits_m1);
	put8(tc);
	put8(nkeys);
}

static void begin(const char *h)
{
	harness = h;
	bn = 0;
}

static void emit(const char *name)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof path, "%s/%s", dir, harness);
	if (mkdir(path, 0777) && errno != EEXIST) {
		perror(path);
		exit(1);
	}
	snprintf(path, sizeof path, "%s/%s/%s", dir, harness, name);
	f = fopen(path, "wb");
	if (!f || fwrite(buf, 1, bn, f) != bn || fclose(f)) {
		perror(path);
		exit(1);
	}
	bn = 0;
}

static void decode_seed(const char *name, unsigned jog, unsigned faders, size_t n, const uint8_t *msg)
{
	begin("decode");
	map_bytes(jog, faders, 0, 3, 0, 2);
	putlist(n, msg);
	emit(name);
}

#define DECODE(name, jog, faders, ...) do { \
	static const uint8_t m_[] = { __VA_ARGS__ }; \
	decode_seed(name, jog, faders, sizeof m_, m_); } while (0)

static void decode(void)
{
	DECODE("note-off",         0, 0x01ff, 0x80, 0x10, 0x00);
	DECODE("note-unmapped",    0, 0x01ff, 0x90, 0x7f, 0x7f);
	DECODE("note-no-velocity", 0, 0x01ff, 0x90, 0x10);
	DECODE("touch-master",     0, 0x01ff, 0x90, 0x40, 0x7f);
	DECODE("note-on-mute",     0, 0x01ff, 0x90, 0x20, 0x7f);
	DECODE("key-id",           0, 0x01ff, 0x90, 0x52, 0x7f);
	DECODE("note-on-zero",     0, 0x01ff, 0x90, 0x10, 0x00);
	DECODE("note-on-flash",    0, 0x01ff, 0x90, 0x10, 0x01);
	DECODE("note-off-velocity", 0, 0x01ff, 0x80, 0x10, 0x7f);
	DECODE("bend-ch0",         0, 0x01ff, 0xe0, 0x01, 0x02);
	DECODE("bend-channel-9",   0, 0x0200, 0xe9, 0x05, 0x05);
	DECODE("bend-no-msb",      0, 0x01ff, 0xe0, 0x05);
	DECODE("vpot-cw",          0, 0x01ff, 0xb0, 0x22, 0x25);
	DECODE("vpot-ccw",         0, 0x01ff, 0xb0, 0x21, 0x43);
	DECODE("vpot-short",       0, 0x01ff, 0xb0, 0x20);
	DECODE("jog-ccw",          0, 0x01ff, 0xb0, 0x30, 0x41);
	DECODE("jog-short",        0, 0x01ff, 0xb0, 0x30);
	DECODE("jog-none",      0x80, 0x01ff, 0xb0, 0xff, 0x05);
	begin("decode");
	map_bytes(0, 0x01ff, 0, 3, 0, 2);
	put8(0);
	emit("args");
}

/* a sysex op: selector, length-prefixed bytes */
static void sysex_seed(const char *name, unsigned model, unsigned sel, size_t n, const uint8_t *b)
{
	begin("sysex");
	put8(model);
	put8(sel);
	putlist(n, b);
	emit(name);
}

#define SYSEX(name, model, sel, ...) do { \
	static const uint8_t m_[] = { __VA_ARGS__ }; \
	sysex_seed(name, model, sel, sizeof m_, m_); } while (0)

static void sysex(void)
{
	/* framed: the first byte is the sub-command; unframed: the message as it is */
	SYSEX("query",               0x14, 0x80 | 8, 0x00);
	SYSEX("connection-query",    0x14, 0x80 | 8, 0x01, '0', '1', '5', '6', '4', '0', 'B', '0', 'B', '8', 'E');
	SYSEX("serial-only",         0x14, 0x80 | 8, 0x01, '0', '1', '5', '6', '4', '0');
	SYSEX("serial-and-two",      0x14, 0x80 | 8, 0x01, '0', '1', '5', '6', '4', '0', 'B', '0', 'B');
	SYSEX("no-eox",              0x14, 8, 0xf0, 0x00, 0x00, 0x66, 0x14, 0x00, 0x00);
	SYSEX("other-manufacturer",  0x14, 8, 0xf0, 0x00, 0x00, 0x67, 0x14, 0x00, 0xf7);
	SYSEX("other-model",         0x14, 8, 0xf0, 0x00, 0x00, 0x66, 0x15, 0x00, 0xf7);
	SYSEX("short-five",          0xf7, 8, 0xf0, 0x00, 0x00, 0x66, 0xf7);
	SYSEX("args-null",           0x14, 8, 0x01, 0x02, 0x03);
	SYSEX("version-query-reply", 0x14, 0x80 | 8, 0x00);
	SYSEX("version-reply",       0x14, 0x80 | 16, 0x14, 'V', '1', '.', '2', '2');
	SYSEX("version-exact-cap",   0x14, 0x80 | 5, 0x14, 'V', '1', '.', '2', '2');
	SYSEX("response-live",       0x14, 8, '0', 'B', '8', 'E');
	SYSEX("response-short",      0x14, 8, 0x10, 0x20);
	SYSEX("response-high",       0x14, 8, 0x7f, 0x7f, 0x00, 0x00);
}

/* a text op: selector, a 32-bit number, a text */
static void text_seed(const char *name, unsigned digits_m1, unsigned sel, uint32_t num, const char *t)
{
	begin("text");
	map_bytes(0, 0x01ff, 0x10, digits_m1, 0x20, 0);
	put8(sel);
	put32(num);
	puttext(t);
	emit(name);
}

static void text(void)
{
	char full[121];

	memset(full, 'a', 120);
	full[120] = 0;
	text_seed("scribble-del",         3, 8, 0, "\x7f");
	text_seed("scribble-null",        3, 8, 0, "A");
	text_seed("scribble-offset-high", 3, 8, 133, "x");
	text_seed("scribble-past-end",    3, 8, 110, "abcd");
	text_seed("scribble-full",        3, 8, 0, full);
	text_seed("scribble-utf8",        3, 8, 0, "\xc3\xa9");
	text_seed("strip-4-bottom",       3, 8, 4 | 1 << 8, "ab");
	text_seed("strip-long",           3, 8, 0, "abcdefghij");
	text_seed("strip-short",          3, 8, 0, "ab");
	text_seed("strip-astral",         3, 8, 0, "abcde\xf0\x9f\x98\x80");
	text_seed("strip-row-2",          3, 8, 2 << 8, "a");
	text_seed("timecode-text",        3, 8, 0, "01:23");
	text_seed("timecode-short",       3, 8, 0, "7");
	text_seed("timecode-exact-cap",   3, 4, 0, "0123");
	text_seed("timecode-no-room",     3, 2, 0, "0123");
	text_seed("assignment-text",      3, 8, 0, "AB");
	text_seed("assignment-empty",     3, 8, 0, "");
	text_seed("segment-lower",        3, 8, 'z', "");
	text_seed("segment-astral",       3, 8, 0x100df, "");
	text_seed("segment-underscore",   3, 8, '_', "");
}

/* a map op: the base byte, the map's bytes, the keys-NULL byte, edits (index, value), then the
 * four bytes at the end, the first an error code */
static void map_seed(const char *name, unsigned digits_m1, unsigned tc, unsigned assign,
		     unsigned nokeys, int code, unsigned nedits, const uint8_t *edits)
{
	begin("map");
	put8(1);
	map_bytes(0, 0xffff, assign, digits_m1, tc, 2);
	put8(nokeys);
	putn(edits, nedits * 2);
	put8((unsigned)code); put8(0); put8(0); put8(0);
	emit(name);
}

#define MAP(name, digits_m1, tc, assign, nokeys, code, ...) do { \
	static const uint8_t e_[] = { __VA_ARGS__ 0, 0 }; \
	map_seed(name, digits_m1, tc, assign, nokeys, code, (sizeof e_ - 2) / 2, e_); } while (0)

static void map(void)
{
	MAP("map-jog-127",      3, 0, 0, 0, 0, 86, 0x7f, 87, 0x00,);
	MAP("map-jog-128",      3, 0, 0, 0, 0, 86, 0x80, 87, 0x00,);
	MAP("map-jog-minus-2",  3, 0, 0, 0, 0, 86, 0xfe, 87, 0xff,);
	MAP("map-display-edge", 15, 112, 0, 0, 0,);
	MAP("map-assign-126",   3, 0, 126, 0, 0,);
	MAP("map-assign-127",   3, 0, 0, 0, 0, 83, 0x7f,);
	MAP("map-digits-0",     3, 0, 0, 0, 0, 85, 0,);
	MAP("map-digits-17",    3, 0, 0, 0, 0, 85, 17,);
	MAP("map-note-128",     3, 0, 0, 0, 0, 0, 0x80,);
	MAP("map-note-twice",   3, 0, 0, 0, 0, 1, 0x10,);
	MAP("map-key-no-id",    3, 0, 0, 0, 0, 97, 0x02,);
	MAP("map-keys-null",    3, 0, 0, 1, 0,);
	MAP("map-ring-cc-128",  3, 0, 0, 0, 0, 73, 0x80,);
	MAP("map-vpot-cc-128",  3, 0, 0, 0, 0, 65, 0x80,);
	MAP("strerror-range",   3, 0, 0, 0, -3,);
	MAP("strerror-unknown", 3, 0, 0, 0, 5,);
}

/* an encode op: selector, a 32-bit, a number, two byte runs (and a 32-bit more for a fader) */
static void enc_head(unsigned model)
{
	map_bytes(0, 0x01ff, 0, 3, 0, 2);
	put8(model);
}

enum { F_NAN, F_FIN };

static void enc_op(const char *name, unsigned sel, uint32_t a, int fin, int32_t n, const char *r1,
		   const char *r2, uint32_t v14)
{
	begin("encode");
	enc_head(0x14);
	put8(sel);
	put32(a);
	if (fin == F_FIN)
		putfin(n);
	else
		put8(0);
	putlist(strlen(r1), (const uint8_t *)r1);
	putlist(strlen(r2), (const uint8_t *)r2);
	if (sel % 8 == 0)
		put32(v14);
	emit(name);
}

static void encode(void)
{
	const int32_t ONE = 1 << 20;

	enc_op("fader-over",      0, 0, F_FIN, 2 * ONE, "", "", 0);
	enc_op("fader-half",      0, 0, F_FIN, ONE / 2, "", "", 0);
	enc_op("fader-full",      0, 0, F_FIN, ONE, "", "", 0);
	enc_op("fader-channel-9", 0, 9, F_FIN, 0, "", "", 0);
	enc_op("fader14-past",    0, 0, F_FIN, 0, "", "", 16384);
	enc_op("fader14-null",    0, 0, F_NAN, 0, "", "", 5);
	enc_op("led-on",          1 | 1 << 3, 5, F_NAN, 0, "", "", 0);
	enc_op("led-flash",       1 | 2 << 3, 5, F_NAN, 0, "", "", 0);
	enc_op("led-off",         1, 5, F_NAN, 0, "", "", 0);
	enc_op("led-note-high",   1 | 1 << 3, 0x85, F_NAN, 0, "", "", 0);
	enc_op("note-strip-8",    2, 0 | 8 << 8, F_NAN, 0, "", "", 0);
	enc_op("note-button-5",   2, 5, F_NAN, 0, "", "", 0);
	enc_op("note-strip-3",    2, 1 | 3 << 8, F_NAN, 0, "", "", 0);
	enc_op("note-touch-master", 2, 9 << 8, F_NAN, 0, "", "", 0);
	enc_op("ring-mode-4",     3, 0 | 4 << 8, F_FIN, 0, "", "", 0);
	enc_op("ring-encoder-8",  3, 8, F_FIN, 0, "", "", 0);
	enc_op("ring-centre",     3 | 1 << 3, 0, F_FIN, 5 * ONE, "", "", 0);
	enc_op("ring-wrap",       3, 1 | 2 << 8, F_FIN, 3 * ONE, "", "", 0);
	enc_op("ring-over",       3, 0, F_FIN, 12 * ONE, "", "", 0);
	enc_op("meter-overload-3", 4 | 3 << 3, 0, F_FIN, 0, "", "", 0);
	enc_op("meter-set",       4 | 1 << 3, 0, F_FIN, 0, "", "", 0);
	enc_op("meter-clear",     4 | 2 << 3, 0, F_FIN, 0, "", "", 0);
	enc_op("meter-strip-9",   4, 9, F_FIN, 3 * ONE, "", "", 0);
	enc_op("meter-over",      4, 0, F_FIN, 13 * ONE, "", "", 0);
	enc_op("colours-ok",      5, 0, F_NAN, 0, "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10", "", 0);
	enc_op("colours-nine",    5, 0, F_NAN, 0, "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x09", "", 0);
	enc_op("colours-eight",   5, 0, F_NAN, 0, "\x09\x0a\x0b\x0c\x0d\x0e\x0f\x11", "", 0);
	enc_op("colour-prefix",   5, 0, F_NAN, 0, "redx", "", 0);
	enc_op("colour-magenta",  5 | 0x40, 0, F_NAN, 0, "\x14", "", 0);
	enc_op("colour-yellow",   5 | 0x40, 0, F_NAN, 0, "\x12", "", 0);
	enc_op("reply-long-serial",   6, 0, F_NAN, 0, "123456789", "ab", 0);
	enc_op("reply-long-response", 6, 0, F_NAN, 0, "abc", "123456", 0);
	enc_op("reply-high-bytes",    6, 0, F_NAN, 0, "\x80\x81", "\xf0", 0);
	enc_op("reply-null",          6, 0, F_NAN, 0, "s", "r", 0);
	enc_op("fixed",           7, 0x14, F_NAN, 0, "", "", 0);
}

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <corpus dir>\n", argv[0]);
		return 2;
	}
	dir = argv[1];
	if (mkdir(dir, 0777) && errno != EEXIST) {
		perror(dir);
		return 1;
	}
	decode();
	sysex();
	text();
	map();
	encode();
	return 0;
}

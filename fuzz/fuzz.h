// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* What every libFuzzer harness in fuzz/ shares: a cursor over the input, the input-size
 * refusal, the exact-size copy that lets the sanitizer see a read one byte past a message,
 * and the control map the harnesses build from the input.
 *
 * The cursor is TOTAL. Reading past the end of the input yields zeros and an empty slice,
 * never a failure, so every input decodes to some sequence of calls and the end of the input
 * ends the sequence. Nothing here reads a clock, the environment, a file or the network, and
 * nothing allocates except fuzz_alloc. */
#ifndef MACKIE_FUZZ_H
#define MACKIE_FUZZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mackie/mackie.h>

#include "spec.h"

/* The OSS Verify driver refuses an input file this large; the harnesses refuse it too, so
 * the domain they are proven over is the domain they are run over. */
#define FUZZ_INPUT_MAX ((size_t)64 << 20)

/* A property check: a function the sabotage rule can name (fuzz/sabotage.sh finds the
 * innermost check_ function of an abort's stack), so it is never inlined away. */
#define FUZZ_CHECK __attribute__((noinline)) static void

struct fuzz_in {
	const uint8_t *p;
	size_t n;
};

static inline struct fuzz_in fuzz_in(const uint8_t *data, size_t size)
{
	struct fuzz_in in = { data, size };
	return in;
}

static inline uint8_t fuzz_u8(struct fuzz_in *in)
{
	if (in->n == 0)
		return 0;
	in->n--;
	return *in->p++;
}

/* Little-endian, like every multi-byte field the harnesses invent. */
static inline uint16_t fuzz_u16(struct fuzz_in *in)
{
	uint16_t lo = fuzz_u8(in);
	return (uint16_t)(lo | (uint16_t)fuzz_u8(in) << 8);
}

static inline uint32_t fuzz_u32(struct fuzz_in *in)
{
	uint32_t lo = fuzz_u16(in);
	return lo | (uint32_t)fuzz_u16(in) << 16;
}

/* Up to `want` bytes from the cursor; returns how many there were. */
static inline size_t fuzz_take(struct fuzz_in *in, size_t want, const uint8_t **out)
{
	size_t n = want < in->n ? want : in->n;
	*out = in->p;
	in->p += n;
	in->n -= n;
	return n;
}

/* A heap block of exactly `n` bytes (one for n = 0). A message handed to a decoder lives in
 * its own allocation, so a read past its end lands in a redzone the sanitizer watches
 * instead of in the next record of the same input. */
static inline uint8_t *fuzz_alloc(size_t n)
{
	uint8_t *b = malloc(n ? n : 1);
	/* not a property: the harness's own allocation failed */
	if (!b)
		abort();
	return b;
}

static inline uint8_t *fuzz_dup(const uint8_t *p, size_t n)
{
	uint8_t *b = fuzz_alloc(n);
	if (n)
		memcpy(b, p, n);
	return b;
}

/* Whether each of the `n` bytes at `p` is still `fill`: a buffer the library must not write
 * past, filled before the call and read after it. */
static inline int fuzz_all(const uint8_t *p, size_t n, uint8_t fill)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != fill)
			return 0;
	return 1;
}

/* A record: a length byte, then that many bytes (or what is left), in an allocation of
 * exactly their length. */
static inline uint8_t *fuzz_msg(struct fuzz_in *in, size_t max, size_t *len)
{
	const uint8_t *p;
	*len = fuzz_take(in, fuzz_u8(in) % (max + 1), &p);
	return fuzz_dup(p, *len);
}

/* NUL-terminated text: a u16 length, then that many bytes (or what is left), and a NUL after
 * them, in an allocation of exactly that. A NUL in the bytes ends the text early. */
static inline char *fuzz_text(struct fuzz_in *in)
{
	const uint8_t *p;
	size_t n = fuzz_take(in, fuzz_u16(in), &p);
	char *t = (char *)fuzz_alloc(n + 1);
	if (n)
		memcpy(t, p, n);
	t[n] = '\0';
	return t;
}

/* A number the encoders round: not a number, past either end, or n / 2^20 for a 32-bit n,
 * exact in a double, so the harness rounds it in integers. */
struct fuzz_num {
	int kind;    /* SPEC_NUM_* */
	int64_t n;   /* the numerator, for SPEC_NUM_FINITE */
	double d;
};

static inline struct fuzz_num fuzz_num(struct fuzz_in *in)
{
	struct fuzz_num v = { SPEC_NUM_FINITE, 0, 0 };
	switch (fuzz_u8(in) % 8) {
	case 0: v.kind = SPEC_NUM_NAN; v.d = __builtin_nan(""); break;
	case 1: v.kind = SPEC_NUM_POS; v.d = __builtin_inf(); break;
	case 2: v.kind = SPEC_NUM_NEG; v.d = -__builtin_inf(); break;
	case 3: v.kind = SPEC_NUM_POS; v.d = 1e300; break;
	case 4: v.kind = SPEC_NUM_NEG; v.d = -1e300; break;
	default:
		v.n = (int32_t)fuzz_u32(in);
		v.d = (double)v.n / 1048576.0;
	}
	return v;
}

/* A control map every encoder and decoder can use (mackie_map_check accepts it): the input
 * chooses where the notes start and how they step, an odd step so all of them differ mod 128,
 * the CCs, which fader channels there are, the display widths and up to eight further keys.
 * The vpot and ring CCs and the jog CC differ from one another as well. */
#define FUZZ_KEYS 8

struct fuzz_map {
	struct mackie_map m;
	struct mackie_key keys[FUZZ_KEYS];
};

static const char *const fuzz_key_ids[FUZZ_KEYS] = {
	"k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7"
};

static inline void fuzz_map_init(struct fuzz_map *fm, struct fuzz_in *in)
{
	unsigned nb = fuzz_u8(in), ns = fuzz_u8(in) | 1u;
	unsigned cb = fuzz_u8(in), cs = fuzz_u8(in) | 1u;
	unsigned i = 0;
	struct mackie_map *m = &fm->m;

	memset(fm, 0, sizeof *fm);
	for (unsigned b = 0; b < MACKIE_STRIP_BUTTONS; b++)
		for (unsigned s = 0; s < MACKIE_STRIPS; s++, i++)
			m->strip_note[b][s] = (uint8_t)((nb + i * ns) & 0x7f);
	for (unsigned s = 0; s <= MACKIE_STRIPS; s++, i++)
		m->touch_note[s] = (uint8_t)((nb + i * ns) & 0x7f);
	for (unsigned g = 0; g < MACKIE_GLOBALS; g++, i++)
		m->global_note[g] = (uint8_t)((nb + i * ns) & 0x7f);
	for (unsigned e = 0; e < MACKIE_STRIPS; e++) {
		m->vpot_cc[e] = (uint8_t)((cb + e * cs) & 0x7f);
		m->ring_cc[e] = (uint8_t)((cb + (MACKIE_STRIPS + e) * cs) & 0x7f);
	}
	m->jog_cc = fuzz_u8(in) & 0x80 ? -1 : (int16_t)((cb + 2 * MACKIE_STRIPS * cs) & 0x7f);
	m->fader_channels = fuzz_u16(in);
	m->assignment_cc = (uint8_t)(fuzz_u8(in) % 0x7f);
	m->timecode_digits = (uint8_t)(1 + fuzz_u8(in) % MACKIE_DISPLAY_MAX_DIGITS);
	m->timecode_cc = (uint8_t)(fuzz_u8(in) % (0x80 - m->timecode_digits + 1));
	m->n_keys = fuzz_u8(in) % (FUZZ_KEYS + 1);
	for (size_t k = 0; k < m->n_keys; k++, i++) {
		fm->keys[k].note = (uint8_t)((nb + i * ns) & 0x7f);
		fm->keys[k].id = fuzz_key_ids[k];
	}
	m->keys = m->n_keys ? fm->keys : NULL;
}

#endif /* MACKIE_FUZZ_H */

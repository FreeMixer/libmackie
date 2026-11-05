// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libFuzzer harness: THE CHANNEL MESSAGES A SURFACE SENDS (src/mackie_codec.c): the button
 * (Note On and Off), fader (Pitch Bend), V-Pot and jog (Control Change) decoders, which read
 * the bytes of a MIDI message against the control map.
 *
 * Input: seven-odd bytes that choose the map (fuzz_map_init in fuzz.h), then messages: a length
 * byte (the low 4 bits, at most 15 bytes) and that many bytes, until the input ends. Each
 * message is copied into an allocation of exactly its length, so a read past it is a
 * sanitizer report, and handed to all four decoders. The map's notes and CCs all differ, as
 * mackie_map_check requires, so a note or CC names one control.
 *
 * The harness reads each message itself, from the Mackie Control wire format (fuzz/spec.h),
 * and every check holds the library to that reading. No expected value comes from the library.
 *
 * Properties, each an abort() in a check_ function:
 *   - button: only a Note On or Off of at least two bytes on a note the map names is a button,
 *     of the kind and strip that note names (and the key's id for a key); pressed is a Note On
 *     with a velocity above 0, a missing velocity reading as 0; any other message is
 *     MACKIE_E_NOT_MINE and *out is left as it was.
 *   - fader: only a Pitch Bend on a channel the map declares a fader is one; its channel is
 *     the low nibble, its value (msb << 7) | lsb with a missing data byte 0, its position the
 *     value over 16383. The value is held where both data bytes are 7-bit: a byte with the
 *     high bit set is not MIDI data.
 *   - vpot: only a Control Change of three bytes on a CC the map gives an encoder is a V-Pot
 *     turn; the delta is the sign-and-magnitude of the data byte (bit 6 counter-clockwise,
 *     the low 6 bits the ticks).
 *   - jog: the same for the map's jog CC, none at all when the map has none.
 *   - args: a NULL map, out or message with a length is MACKIE_E_ARG.
 */
#include <mackie/mackie_codec.h>

#include "fuzz.h"

/* What a note names on the map: a strip button, a fader touch, a global key or a map key. The
 * notes all differ, so the first is the only one. */
static int owner_of(const struct mackie_map *m, unsigned note, struct spec_owner *w)
{
	for (unsigned k = 0; k < MACKIE_STRIP_BUTTONS; k++)
		for (unsigned s = 0; s < SPEC_STRIPS; s++)
			if (m->strip_note[k][s] == note) {
				w->kind = SPEC_BTN_STRIP0 + (int)k;
				w->strip = (int)s;
				w->id = NULL;
				return 1;
			}
	for (unsigned s = 0; s <= SPEC_STRIPS; s++)
		if (m->touch_note[s] == note) {
			w->kind = SPEC_BTN_TOUCH;
			w->strip = (int)s;
			w->id = NULL;
			return 1;
		}
	for (unsigned g = 0; g < MACKIE_GLOBALS; g++)
		if (m->global_note[g] == note) {
			w->kind = SPEC_BTN_GLOBAL + (int)g;
			w->strip = -1;
			w->id = NULL;
			return 1;
		}
	for (size_t k = 0; k < m->n_keys; k++)
		if (m->keys[k].note == note) {
			w->kind = SPEC_BTN_KEY;
			w->strip = -1;
			w->id = m->keys[k].id;
			return 1;
		}
	return 0;
}

FUZZ_CHECK check_button(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	struct mackie_button b, was;
	struct spec_owner w = { 0, 0, NULL };
	int note_msg = len >= 2 && ((msg[0] & 0xf0) == SPEC_NOTE_ON || (msg[0] & 0xf0) == SPEC_NOTE_OFF);
	int mine = note_msg && owner_of(&fm->m, msg[1], &w);

	memset(&b, 0xa5, sizeof b);
	was = b;
	int rc = mackie_button_decode(&fm->m, msg, len, &b);

	/* A message that is not a Note On or Off of two bytes, or whose note the map does not
	 * name, is not a button, and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_button_decode; mackie.h MACKIE_E_NOT_MINE
	 * mutant: src/mackie_codec.c:107 `!lookup(m, msg[1], &b)` -> `0`
	 * input: note-unmapped */
	if (!mine && (rc != SPEC_E_NOT_MINE || memcmp(&b, &was, sizeof b)))
		abort();
	if (!mine)
		return;
	/* A note the map names is a button.
	 * contract: include/mackie/mackie_codec.h mackie_button_decode
	 * mutant: src/mackie_codec.c:102 `len < 2` -> `len < 3`
	 * input: note-no-velocity
	 * mutant: src/mackie_codec.c:86 `m->keys[k].note == note` -> `m->keys[k].note == (note | 1)`
	 * input: key-id
	 * mutant: src/mackie_codec.c:105 `type != MACKIE_NOTE_ON && type != MACKIE_NOTE_OFF` -> `type != MACKIE_NOTE_ON`
	 * input: note-off */
	if (rc != SPEC_OK)
		abort();
	/* The note names the kind and the strip the map gives it, and a key its own id.
	 * contract: include/mackie/mackie.h struct mackie_map; mackie_codec.h struct mackie_button
	 * mutant: src/mackie_codec.c:69 `b->strip = MACKIE_STRIPS;` -> `b->strip = -1;`
	 * input: touch-master
	 * mutant: src/mackie_codec.c:80 `(enum mackie_button_kind)(MACKIE_BTN_REC + k)` -> `(enum mackie_button_kind)(MACKIE_BTN_REC + k + 1)`
	 * input: note-on-mute */
	if ((int)b.kind != w.kind || b.strip != w.strip || b.id != w.id)
		abort();
}

FUZZ_CHECK check_button_pressed(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	struct mackie_button b;
	struct spec_owner w;

	if (len < 2 || ((msg[0] & 0xf0) != SPEC_NOTE_ON && (msg[0] & 0xf0) != SPEC_NOTE_OFF) ||
	    !owner_of(&fm->m, msg[1], &w))
		return;
	if (mackie_button_decode(&fm->m, msg, len, &b) != SPEC_OK)
		return;
	/* Pressed is a Note On with a velocity above 0; a Note Off, a Note On at 0 and a Note On
	 * with no velocity byte are released.
	 * contract: include/mackie/mackie_codec.h mackie_button_decode
	 * mutant: src/mackie_codec.c:110 `msg[2] > 0` -> `msg[2] >= 0`
	 * input: note-on-zero
	 * mutant: src/mackie_codec.c:110 `msg[2] > 0` -> `msg[2] > 1`
	 * input: note-on-flash
	 * mutant: src/mackie_codec.c:110 `type == MACKIE_NOTE_ON && ` -> ``
	 * input: note-off-velocity */
	if (b.pressed != ((msg[0] & 0xf0) == SPEC_NOTE_ON && len > 2 && msg[2] > 0))
		abort();
}

FUZZ_CHECK check_fader(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	struct mackie_fader f, was;
	int bend = len >= 1 && (msg[0] & 0xf0) == SPEC_BEND;
	int mine = bend && (fm->m.fader_channels >> (msg[0] & 0x0f) & 1);

	memset(&f, 0xa5, sizeof f);
	was = f;
	int rc = mackie_fader_decode(&fm->m, msg, len, &f);

	/* Only a Pitch Bend on a channel the map declares a fader is one; any other message is
	 * of another kind and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_fader_decode; mackie.h struct mackie_map fader_channels
	 * mutant: src/mackie_codec.c:43 `(msg[0] & 0xf0) != MACKIE_PITCH_BEND` -> `0`
	 * input: note-off */
	if (!mine && (rc != SPEC_E_NOT_MINE || memcmp(&f, &was, sizeof f)))
		abort();
	if (!mine)
		return;
	/* The channel is the status byte's low nibble.
	 * contract: Mackie Control: one pitch bend channel per fader; mackie_codec.h struct mackie_fader
	 * mutant: src/mackie_codec.c:48 `msg[0] & 0x0f` -> `msg[0] & 0x07`
	 * input: bend-channel-9
	 * mutant: src/mackie_codec.c:45 `1u << (msg[0] & 0x0f)` -> `1u << (msg[0] & 0x07)`
	 * input: bend-channel-9
	 * mutant: src/mackie_codec.c:43 `(msg[0] & 0xf0) != MACKIE_PITCH_BEND` -> `(msg[0] & 0xf0) != MACKIE_NOTE_ON`
	 * input: bend-ch0 */
	if (rc != SPEC_OK || f.channel != (msg[0] & 0x0f))
		abort();
	unsigned lsb = len > 1 ? msg[1] : 0, msb = len > 2 ? msg[2] : 0;
	int data7 = lsb < 0x80 && msb < 0x80;
	/* In 7-bit data bytes, the value is the most significant byte shifted by 7 over the least
	 * significant, a missing byte reading as 0, and the position is the value over 16383.
	 * contract: include/mackie/mackie_codec.h mackie_fader_decode; mackie.h MACKIE_PITCHBEND_MAX
	 * mutant: src/mackie_codec.c:49 `(msb << 7) | lsb` -> `(msb << 6) | lsb`
	 * input: bend-ch0
	 * mutant: src/mackie_codec.c:47 `len > 2 ? msg[2] : 0` -> `len > 2 ? msg[2] : 1`
	 * input: bend-no-msb
	 * mutant: src/mackie_codec.c:50 `/ MACKIE_PITCHBEND_MAX` -> `/ 16384`
	 * input: bend-ch0 */
	if (data7 && (f.value != (msb << 7 | lsb) || f.position != (double)(msb << 7 | lsb) / SPEC_BEND_MAX))
		abort();
}

FUZZ_CHECK check_vpot(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	struct mackie_vpot v, was;
	int enc = -1;

	if (len >= 3 && (msg[0] & 0xf0) == SPEC_CC)
		for (int e = 0; e < SPEC_STRIPS; e++)
			if (fm->m.vpot_cc[e] == msg[1])
				enc = e;
	memset(&v, 0xa5, sizeof v);
	was = v;
	int rc = mackie_vpot_decode(&fm->m, msg, len, &v);

	/* A Control Change of three bytes on a CC the map gives an encoder is a V-Pot turn;
	 * nothing else is, and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_vpot_decode
	 * mutant: src/mackie_codec.c:158 `len < 3` -> `len < 2`
	 * input: vpot-short */
	if (enc < 0 && (rc != SPEC_E_NOT_MINE || memcmp(&v, &was, sizeof v)))
		abort();
	if (enc < 0)
		return;
	/* The encoder is the one the CC names; the delta is the sign and magnitude of the data
	 * byte: bit 6 set is counter-clockwise, the low 6 bits are the ticks.
	 * contract: include/mackie/mackie_codec.h mackie_vpot_decode ("MCU relative sign+magnitude")
	 * mutant: src/mackie_codec.c:150 `value & 0x40 ? -ticks : ticks` -> `value & 0x20 ? -ticks : ticks`
	 * input: vpot-ccw
	 * mutant: src/mackie_codec.c:149 `value & 0x3f` -> `value & 0x1f`
	 * input: vpot-cw
	 * mutant: src/mackie_codec.c:162 `(uint8_t)e` -> `(uint8_t)(e ^ 1)`
	 * input: vpot-cw
	 * mutant: src/mackie_codec.c:161 `m->vpot_cc[e] == msg[1]` -> `m->vpot_cc[e] != msg[1]`
	 * input: vpot-cw */
	if (rc != SPEC_OK || v.encoder != enc || v.delta != ((msg[2] & 0x40) ? -(msg[2] & 0x3f) : (msg[2] & 0x3f)))
		abort();
}

FUZZ_CHECK check_jog(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	int delta = 0x5a5a5a5a;
	int mine = len >= 3 && (msg[0] & 0xf0) == SPEC_CC && fm->m.jog_cc >= 0 &&
		   msg[1] == fm->m.jog_cc;
	int rc = mackie_jog_decode(&fm->m, msg, len, &delta);

	/* Only a Control Change of three bytes on the map's jog CC is a jog turn, and a map with
	 * no jog has none; the result is left as it was otherwise.
	 * contract: include/mackie/mackie_codec.h mackie_jog_decode
	 * mutant: src/mackie_codec.c:175 `m->jog_cc < 0 || msg[1] != m->jog_cc` -> `msg[1] != (uint8_t)m->jog_cc`
	 * input: jog-none
	 * mutant: src/mackie_codec.c:173 `len < 3` -> `len < 2`
	 * input: jog-short */
	if (!mine && (rc != SPEC_E_NOT_MINE || delta != 0x5a5a5a5a))
		abort();
	if (!mine)
		return;
	/* The delta reads as a V-Pot's does.
	 * contract: include/mackie/mackie_codec.h mackie_jog_decode
	 * mutant: src/mackie_codec.c:177 `relative(msg[2])` -> `msg[2] & 0x3f`
	 * input: jog-ccw */
	if (rc != SPEC_OK || delta != ((msg[2] & 0x40) ? -(msg[2] & 0x3f) : (msg[2] & 0x3f)))
		abort();
}

FUZZ_CHECK check_args(const struct fuzz_map *fm, const uint8_t *msg, size_t len)
{
	struct mackie_button b;
	struct mackie_fader f;
	struct mackie_vpot v;
	int d;

	/* A NULL map, a NULL result, or a NULL message with a length is an argument error, from
	 * every decoder.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_codec.c:42 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: args */
	if (mackie_button_decode(NULL, msg, len, &b) != SPEC_E_ARG ||
	    mackie_button_decode(&fm->m, msg, len, NULL) != SPEC_E_ARG ||
	    mackie_fader_decode(NULL, msg, len, &f) != SPEC_E_ARG ||
	    mackie_fader_decode(&fm->m, msg, len, NULL) != SPEC_E_ARG ||
	    mackie_vpot_decode(NULL, msg, len, &v) != SPEC_E_ARG ||
	    mackie_vpot_decode(&fm->m, msg, len, NULL) != SPEC_E_ARG ||
	    mackie_jog_decode(NULL, msg, len, &d) != SPEC_E_ARG ||
	    mackie_jog_decode(&fm->m, msg, len, NULL) != SPEC_E_ARG ||
	    (len && (mackie_button_decode(&fm->m, NULL, len, &b) != SPEC_E_ARG ||
		     mackie_fader_decode(&fm->m, NULL, len, &f) != SPEC_E_ARG ||
		     mackie_vpot_decode(&fm->m, NULL, len, &v) != SPEC_E_ARG ||
		     mackie_jog_decode(&fm->m, NULL, len, &d) != SPEC_E_ARG)))
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_in in;
	struct fuzz_map fm;

	if (size >= FUZZ_INPUT_MAX)
		return -1;
	in = fuzz_in(data, size);
	fuzz_map_init(&fm, &in);
	while (in.n > 0) {
		size_t len;
		uint8_t *msg = fuzz_msg(&in, 15, &len);

		check_button(&fm, msg, len);
		check_button_pressed(&fm, msg, len);
		check_fader(&fm, msg, len);
		check_vpot(&fm, msg, len);
		check_jog(&fm, msg, len);
		check_args(&fm, msg, len);
		free(msg);
	}
	return 0;
}

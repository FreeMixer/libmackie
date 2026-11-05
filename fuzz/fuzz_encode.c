// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* libFuzzer harness: THE MESSAGES A HOST SENDS (src/mackie_codec.c and src/mackie_sysex.c):
 * the fader, button-LED, V-Pot ring and meter encoders, the note lookups, the scribble
 * backlight colours and the fixed SysEx (reset, device query, version request, connection
 * reply). Each takes numbers or bytes the host chooses and writes one struct mackie_msg.
 *
 * Input: the bytes that choose the map (fuzz_map_init in fuzz.h), a model id, then operations
 * until the input ends: a selector byte, the numbers it needs (a number the encoders round is
 * one byte that picks not-a-number, either infinity, a huge value, or a 32-bit n read as
 * n / 2^20, which a double holds exactly), and for the colours and the connection reply a
 * bounded run of bytes in an allocation of exactly that size.
 *
 * The harness writes the expected message itself, from the Mackie Control wire format and the
 * live capture (fuzz/spec.h; the rounding is done in integers), and every check holds the
 * library to that. No expected value comes from the library.
 *
 * Properties, each an abort() in a check_ function:
 *   - fader: a position clamped into [0, 1] and rounded to 14 bits, a value past 16383
 *     refused as MACKIE_E_RANGE with the result untouched, as the Pitch Bend E<channel & 15>
 *     lsb msb.
 *   - led: Note On <note & 7f> with 7F on, 01 flashing, 00 off.
 *   - notes: the strip note and the touch note the map holds, a strip past the last
 *     MACKIE_E_RANGE and the touch past the strips the master's.
 *   - ring: B0 <ring CC> <centre<<6 | mode<<4 | position 0..11>; a mode past the four
 *     MACKIE_E_ARG, an encoder past the last MACKIE_E_RANGE.
 *   - meter: D0 <strip & 7, level 0..12 or the overload nibble E or F>; an overload past the
 *     three MACKIE_E_ARG.
 *   - colours: F0 00 00 66 <model> 72 and eight bytes and F7 for exactly eight indexes below
 *     8, MACKIE_E_RANGE for anything else; a colour's index is its place in the wire order.
 *   - fixed: reset, device query and version request are the bytes the live exchange shows.
 *   - reply: a connection reply is F0 00 00 66 <model> 02, at most 7 bytes of the serial, at
 *     most 4 of the response, F7; a NULL result or a length with no bytes is MACKIE_E_ARG.
 */
#include <mackie/mackie_codec.h>

#include "fuzz.h"

static const char *const colour_names[SPEC_COLOURS] = {
	"off", "red", "green", "yellow", "blue", "magenta", "cyan", "white"
};

FUZZ_CHECK check_fader(unsigned channel, struct fuzz_num p, unsigned value14)
{
	struct mackie_msg out, was;
	unsigned v = spec_scaled(p.kind, p.n, 1, SPEC_BEND_MAX);

	memset(&out, 0xa5, sizeof out);
	was = out;
	int rc = mackie_fader_encode(channel, p.d, &out);

	/* A position is clamped into [0, 1] and rounded to the nearest of 16384 steps, a half
	 * up, NaN as 0, and goes out as a Pitch Bend: E<channel & 15>, the low 7 bits, the high.
	 * contract: include/mackie/mackie_codec.h mackie_fader_encode
	 * mutant: src/mackie_codec.c:32 `position < 0 ? 0 : position > 1 ? 1 : position` -> `position < 0 ? 0 : position`
	 * input: fader-over
	 * mutant: src/mackie_round.h:18 `x - r >= 0.5` -> `x - r > 0.5`
	 * input: fader-half
	 * mutant: src/mackie_codec.c:33 `p * MACKIE_PITCHBEND_MAX` -> `p * 16384`
	 * input: fader-full
	 * mutant: src/mackie_codec.c:25 `(value >> 7) & 0x7f` -> `(value >> 8) & 0x7f`
	 * input: fader-full
	 * mutant: src/mackie_codec.c:25 `MACKIE_PITCH_BEND | (channel & 0x0f)` -> `MACKIE_PITCH_BEND | (channel & 0x07)`
	 * input: fader-channel-9 */
	if (rc != SPEC_OK || out.len != 3 || out.bytes[0] != (SPEC_BEND | (channel & 0x0f)) ||
	    out.bytes[1] != (v & 0x7f) || out.bytes[2] != (v >> 7))
		abort();
	memset(&out, 0xa5, sizeof out);
	was = out;
	rc = mackie_fader_encode14(channel, value14, &out);
	/* A 14-bit value past 16383 is out of range and the result is left as it was.
	 * contract: include/mackie/mackie.h MACKIE_PITCHBEND_MAX; mackie_codec.h mackie_fader_encode14
	 * mutant: src/mackie_codec.c:23 `value > MACKIE_PITCHBEND_MAX` -> `value > MACKIE_PITCHBEND_MAX + 1`
	 * input: fader14-past */
	if ((value14 > SPEC_BEND_MAX && (rc != SPEC_E_RANGE || memcmp(&out, &was, sizeof out))) ||
	    (value14 <= SPEC_BEND_MAX && (rc != SPEC_OK || out.len != 3 || out.bytes[1] != (value14 & 0x7f) ||
					  out.bytes[2] != (value14 >> 7))))
		abort();
	/* A NULL result is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_codec.c:22 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: fader14-null */
	if (mackie_fader_encode14(channel, value14, NULL) != SPEC_E_ARG ||
	    mackie_fader_encode(channel, p.d, NULL) != SPEC_E_ARG)
		abort();
}

FUZZ_CHECK check_led(unsigned note, unsigned state)
{
	struct mackie_msg out;
	unsigned v = state == 1 ? 0x7f : state == 2 ? 0x01 : 0x00;

	memset(&out, 0xa5, sizeof out);
	int rc = mackie_button_led_encode(note, (enum mackie_led)state, &out);

	/* A button LED is a Note On with the velocity 7F lit, 01 flashing and 00 off, the note's
	 * low 7 bits; a state the enum does not hold is off.
	 * contract: include/mackie/mackie.h MACKIE_LED_ON MACKIE_LED_FLASH MACKIE_LED_OFF
	 * mutant: src/mackie_codec.c:119 `state == MACKIE_LED_STATE_ON ? MACKIE_LED_ON` -> `state == MACKIE_LED_STATE_ON ? MACKIE_LED_FLASH`
	 * input: led-on
	 * mutant: src/mackie_codec.c:120 `state == MACKIE_LED_STATE_FLASH ? MACKIE_LED_FLASH` -> `state == MACKIE_LED_STATE_FLASH ? MACKIE_LED_ON`
	 * input: led-flash
	 * mutant: src/mackie_codec.c:121 `note & 0x7f` -> `note`
	 * input: led-note-high
	 * mutant: src/mackie_codec.c:120 `: MACKIE_LED_OFF` -> `: MACKIE_LED_ON`
	 * input: led-off */
	if (rc != SPEC_OK || out.len != 3 || out.bytes[0] != SPEC_NOTE_ON || out.bytes[1] != (note & 0x7f) ||
	    out.bytes[2] != v)
		abort();
	/* A NULL result is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_codec.c:118 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: led-on */
	if (mackie_button_led_encode(note, (enum mackie_led)state, NULL) != SPEC_E_ARG)
		abort();
}

FUZZ_CHECK check_notes(const struct fuzz_map *fm, unsigned button, unsigned strip)
{
	uint8_t note = 0xa5;
	int rc = mackie_strip_note(&fm->m, (enum mackie_strip_button)button, strip, &note);

	int bad_pos = button >= MACKIE_STRIP_BUTTONS || strip >= SPEC_STRIPS;
	/* A strip note is the map's, and a button or strip past the last is out of range with
	 * the result untouched.
	 * contract: include/mackie/mackie_codec.h mackie_strip_note
	 * mutant: src/mackie_codec.c:130 `strip >= MACKIE_STRIPS` -> `strip > MACKIE_STRIPS`
	 * input: note-strip-8
	 * mutant: src/mackie_codec.c:130 `(unsigned)button >= MACKIE_STRIP_BUTTONS` -> `(unsigned)button > MACKIE_STRIP_BUTTONS`
	 * input: note-button-5
	 * mutant: src/mackie_codec.c:132 `m->strip_note[button][strip]` -> `m->strip_note[button][strip ^ 1]`
	 * input: note-strip-3 */
	if ((bad_pos && (rc != SPEC_E_RANGE || note != 0xa5)) ||
	    (!bad_pos && (rc != SPEC_OK || note != fm->m.strip_note[button][strip])))
		abort();
	note = 0xa5;
	rc = mackie_touch_note(&fm->m, strip, &note);
	/* The touch note of a strip is the map's; a strip past the last is the master's.
	 * contract: include/mackie/mackie_codec.h mackie_touch_note
	 * mutant: src/mackie_codec.c:140 `strip < MACKIE_STRIPS ? strip : MACKIE_STRIPS` -> `strip < MACKIE_STRIPS ? strip : 0`
	 * input: note-strip-8 */
	if (rc != SPEC_OK || note != fm->m.touch_note[strip < SPEC_STRIPS ? strip : SPEC_STRIPS])
		abort();
	/* A NULL map or result is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_codec.c:129 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: note-strip-3 */
	if (mackie_strip_note(NULL, (enum mackie_strip_button)button, strip, &note) != SPEC_E_ARG ||
	    mackie_strip_note(&fm->m, (enum mackie_strip_button)button, strip, NULL) != SPEC_E_ARG ||
	    mackie_touch_note(NULL, strip, &note) != SPEC_E_ARG ||
	    mackie_touch_note(&fm->m, strip, NULL) != SPEC_E_ARG)
		abort();
}

FUZZ_CHECK check_ring(const struct fuzz_map *fm, unsigned enc, struct fuzz_num p, unsigned mode, int center)
{
	struct mackie_msg out, was;
	unsigned pos = spec_scaled(p.kind, p.n, SPEC_RING_MAX, 1);

	memset(&out, 0xa5, sizeof out);
	was = out;
	int rc = mackie_ring_encode(&fm->m, enc, p.d, (enum mackie_ring_mode)mode, center, &out);

	/* A mode past the four is an argument error and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_ring_encode
	 * mutant: src/mackie_codec.c:184 `(unsigned)mode > MACKIE_RING_SPREAD` -> `(unsigned)mode > MACKIE_RING_SPREAD + 1`
	 * input: ring-mode-4 */
	if (mode > 3 && (rc != SPEC_E_ARG || memcmp(&out, &was, sizeof out)))
		abort();
	if (mode > 3)
		return;
	/* An encoder past the last is out of range and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_ring_encode
	 * mutant: src/mackie_codec.c:186 `encoder >= MACKIE_STRIPS` -> `encoder > MACKIE_STRIPS`
	 * input: ring-encoder-8 */
	if (enc >= SPEC_STRIPS && (rc != SPEC_E_RANGE || memcmp(&out, &was, sizeof out)))
		abort();
	if (enc >= SPEC_STRIPS)
		return;
	/* The ring is a Control Change on the encoder's ring CC, its data the centre LED in bit 6,
	 * the mode in bits 5 and 4 and the position, clamped to 0..11 and rounded, in the low 4.
	 * contract: include/mackie/mackie_codec.h mackie_ring_encode ("bit 6 = centre LED, bits 5-4 = ring mode, bits 3-0 = position")
	 * mutant: src/mackie_codec.c:190 `center ? 0x40u : 0` -> `center ? 0x20u : 0`
	 * input: ring-centre
	 * mutant: src/mackie_codec.c:190 `(unsigned)mode << 4` -> `(unsigned)mode << 3`
	 * input: ring-wrap
	 * mutant: src/mackie_codec.c:188 `position > 11 ? 11 : position` -> `position > 12 ? 12 : position`
	 * input: ring-over
	 * mutant: src/mackie_codec.c:191 `m->ring_cc[encoder]` -> `m->ring_cc[encoder ^ 1]`
	 * input: ring-wrap */
	if (rc != SPEC_OK || out.len != 3 || out.bytes[0] != SPEC_CC || out.bytes[1] != fm->m.ring_cc[enc] ||
	    out.bytes[2] != ((center ? 0x40u : 0) | mode << 4 | pos))
		abort();
}

FUZZ_CHECK check_meter(unsigned strip, struct fuzz_num p, unsigned overload)
{
	struct mackie_msg out, was;
	unsigned nibble = overload == 1 ? 0x0e : overload == 2 ? 0x0f : spec_scaled(p.kind, p.n, SPEC_METER_MAX, 1);

	memset(&out, 0xa5, sizeof out);
	was = out;
	int rc = mackie_meter_encode(strip, p.d, (enum mackie_overload)overload, &out);

	/* An overload past the three is an argument error and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_meter_encode
	 * mutant: src/mackie_codec.c:202 `(unsigned)overload > MACKIE_OVERLOAD_CLEAR` -> `(unsigned)overload > MACKIE_OVERLOAD_CLEAR + 1`
	 * input: meter-overload-3 */
	if (overload > 2 && (rc != SPEC_E_ARG || memcmp(&out, &was, sizeof out)))
		abort();
	if (overload > 2)
		return;
	/* A meter is a Channel Pressure: the strip's low 3 bits over the level, clamped to 0..12
	 * and rounded, or over E to light the clip LED and F to clear it.
	 * contract: include/mackie/mackie_codec.h mackie_meter_encode; mackie.h MACKIE_METER_SEGMENTS
	 * mutant: src/mackie_codec.c:213 `strip & 0x07` -> `strip & 0x0f`
	 * input: meter-strip-9
	 * mutant: src/mackie_codec.c:205 `nibble = 0x0e` -> `nibble = 0x0d`
	 * input: meter-set
	 * mutant: src/mackie_codec.c:207 `nibble = 0x0f` -> `nibble = 0x0e`
	 * input: meter-clear
	 * mutant: src/mackie_codec.c:209 `level > MACKIE_METER_SEGMENTS ? MACKIE_METER_SEGMENTS : level` -> `level > MACKIE_METER_SEGMENTS + 1 ? MACKIE_METER_SEGMENTS + 1 : level`
	 * input: meter-over */
	if (rc != SPEC_OK || out.len != 2 || out.bytes[0] != SPEC_PRESSURE ||
	    out.bytes[1] != (((strip & 7) << 4) | nibble))
		abort();
}

FUZZ_CHECK check_colours(const unsigned *c, size_t n, const char *name, uint8_t model)
{
	struct mackie_msg out, was;
	int ok = n == SPEC_COLOURS;
	int idx = -3;

	for (size_t i = 0; i < n; i++)
		if (c[i] >= SPEC_COLOURS)
			ok = 0;
	memset(&out, 0xa5, sizeof out);
	was = out;
	int rc = mackie_scribble_colours_encode(c, n, model, &out);

	uint8_t want[15] = { SPEC_SYSEX, 0, 0, 0x66, model, SPEC_SUB_COLOUR, 0, 0, 0, 0, 0, 0, 0, 0, SPEC_EOX };
	for (size_t i = 0; ok && i < n; i++)
		want[6 + i] = (uint8_t)c[i];
	/* Exactly eight colours, each an index below 8, make the message, F0 00 00 66 <model> 72
	 * and one byte each and F7; anything else is out of range and the result is left as it was.
	 * contract: include/mackie/mackie_codec.h mackie_scribble_colours_encode; mackie.h MACKIE_SYSEX_SCRIBBLE_COLOUR
	 * mutant: src/mackie_sysex.c:111 `n != MACKIE_STRIPS` -> `n < MACKIE_STRIPS`
	 * input: colours-nine
	 * mutant: src/mackie_sysex.c:114 `colours[i] >= MACKIE_SCRIBBLE_COLOURS` -> `colours[i] > MACKIE_SCRIBBLE_COLOURS`
	 * input: colours-eight
	 * mutant: src/mackie_sysex.c:118 `(uint8_t)colours[i]` -> `(uint8_t)colours[7 - i]`
	 * input: colours-ok */
	if ((ok && (rc != SPEC_OK || out.len != 15 || memcmp(out.bytes, want, 15))) ||
	    (!ok && (rc != SPEC_E_RANGE || memcmp(&out, &was, sizeof out))))
		abort();
	for (int i = 0; i < SPEC_COLOURS; i++)
		if (!strcmp(name, colour_names[i]))
			idx = i;
	/* A colour's index is its place in the wire order off, red, green, yellow, blue, magenta,
	 * cyan, white; a name the table does not carry is out of range.
	 * contract: include/mackie/mackie_codec.h mackie_scribble_colours (wire order)
	 * mutant: src/mackie_sysex.c:101 `strcmp(mackie_scribble_colours[i].name, name) == 0` -> `strncmp(mackie_scribble_colours[i].name, name, 3) == 0`
	 * input: colour-prefix */
	if (mackie_scribble_colour_index(name) != idx || mackie_scribble_colour_index(NULL) != SPEC_E_ARG)
		abort();
	int bad = 0;
	for (int i = 0; i < SPEC_COLOURS; i++) {
		const uint8_t *rgb = mackie_scribble_colours[i].rgb;
		/* the wire order is the bits of the colour: off, R, G, R+G, B, R+B, G+B, white */
		bad |= strcmp(mackie_scribble_colours[i].name, colour_names[i]) ||
		       rgb[0] != (i & 1 ? 255 : 0) || rgb[1] != (i & 2 ? 255 : 0) || rgb[2] != (i & 4 ? 255 : 0);
	}
	/* The table's colours are the ones the surface draws, as RGB.
	 * contract: include/mackie/mackie_codec.h struct mackie_colour ("the RGB it stands for")
	 * mutant: src/mackie_sysex.c:89 `{ "yellow",  { 255, 255, 0 } }` -> `{ "yellow",  { 255, 254, 0 } }`
	 * input: colour-yellow
	 * mutant: src/mackie_sysex.c:91 `{ "magenta", { 255, 0, 255 } }` -> `{ "magenta", { 255, 0, 254 } }`
	 * input: colour-magenta */
	if (bad)
		abort();
	/* A NULL result, or a NULL list with a length, is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_sysex.c:110 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: colours-ok */
	if (mackie_scribble_colours_encode(c, n, model, NULL) != SPEC_E_ARG ||
	    (n && mackie_scribble_colours_encode(NULL, n, model, &out) != SPEC_E_ARG))
		abort();
}

FUZZ_CHECK check_fixed(uint8_t model)
{
	struct mackie_msg q, r, v;
	const uint8_t want_q[] = { 0xf0, 0x00, 0x00, 0x66, model, 0x00, 0xf7 };
	const uint8_t want_r[] = { 0xf0, 0x00, 0x00, 0x66, model, 0x08, 0x00, 0xf7 };
	const uint8_t want_v[] = { 0xf0, 0x00, 0x00, 0x66, model, 0x13, 0x00, 0xf7 };

	memset(&q, 0xa5, sizeof q);
	memset(&r, 0xa5, sizeof r);
	memset(&v, 0xa5, sizeof v);
	/* The device query is F0 00 00 66 <model> 00 F7, the version request ... 13 00 F7 and the
	 * reset ... 08 00 F7, as captured live from an X-Touch.
	 * contract: include/mackie/mackie.h MACKIE_HS_DEVICE_QUERY MACKIE_SYSEX_RESET; mackie_codec.h mackie_device_query_encode mackie_version_request_encode
	 * mutant: src/mackie_sysex.c:181 `head(out, model, MACKIE_HS_DEVICE_QUERY)` -> `head(out, model, MACKIE_HS_HOST_CONNECTION_QUERY)`
	 * input: fixed
	 * mutant: src/mackie_sysex.c:205 `head(out, model, MACKIE_HS_VERSION_REQUEST)` -> `head(out, model, MACKIE_HS_VERSION_REPLY)`
	 * input: fixed
	 * mutant: src/mackie_sysex.c:128 `put(out, 0x00)` -> `put(out, 0x01)`
	 * input: fixed */
	if (mackie_device_query_encode(model, &q) != SPEC_OK || q.len != sizeof want_q || memcmp(q.bytes, want_q, q.len) ||
	    mackie_reset_encode(model, &r) != SPEC_OK || r.len != sizeof want_r || memcmp(r.bytes, want_r, r.len) ||
	    mackie_version_request_encode(model, &v) != SPEC_OK || v.len != sizeof want_v || memcmp(v.bytes, want_v, v.len))
		abort();
	/* A NULL result is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_sysex.c:180 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: fixed */
	if (mackie_device_query_encode(model, NULL) != SPEC_E_ARG || mackie_reset_encode(model, NULL) != SPEC_E_ARG ||
	    mackie_version_request_encode(model, NULL) != SPEC_E_ARG)
		abort();
}

FUZZ_CHECK check_reply(const uint8_t *serial, size_t sn, const uint8_t *resp, size_t rn, uint8_t model)
{
	struct mackie_msg out;
	uint8_t want[6 + SPEC_SERIAL + SPEC_CHALLENGE + 1];
	size_t k = 0, s = sn < SPEC_SERIAL ? sn : SPEC_SERIAL, r = rn < SPEC_CHALLENGE ? rn : SPEC_CHALLENGE;

	want[k++] = SPEC_SYSEX; want[k++] = 0; want[k++] = 0; want[k++] = 0x66; want[k++] = model;
	want[k++] = SPEC_SUB_REPLY;
	memcpy(want + k, serial, s); k += s;
	memcpy(want + k, resp, r); k += r;
	want[k++] = SPEC_EOX;
	memset(&out, 0xa5, sizeof out);
	int rc = mackie_host_connection_reply_encode(serial, sn, resp, rn, model, &out);

	/* The reply is F0 00 00 66 <model> 02, at most 7 bytes of the serial, at most 4 of the
	 * response, F7; what is longer is cut.
	 * contract: include/mackie/mackie_codec.h mackie_host_connection_reply_encode; mackie.h MACKIE_SERIAL_LEN MACKIE_CHALLENGE_LEN
	 * mutant: src/mackie_sysex.c:193 `i < serial_len && i < MACKIE_SERIAL_LEN` -> `i < serial_len`
	 * input: reply-long-serial
	 * mutant: src/mackie_sysex.c:195 `i < response_len && i < MACKIE_CHALLENGE_LEN` -> `i < response_len`
	 * input: reply-long-response
	 * mutant: src/mackie_sysex.c:194 `put(out, serial[i])` -> `put(out, serial[i] & 0x7f)`
	 * input: reply-high-bytes */
	if (rc != SPEC_OK || out.len != k || memcmp(out.bytes, want, k))
		abort();
	/* A NULL result, or a length with no bytes, is an argument error.
	 * contract: include/mackie/mackie_codec.h, "MACKIE_E_ARG for a NULL pointer"; mackie.h
	 * mutant: src/mackie_sysex.c:191 `MACKIE_E_ARG` -> `MACKIE_E_RANGE`
	 * input: reply-null */
	if (mackie_host_connection_reply_encode(serial, sn, resp, rn, model, NULL) != SPEC_E_ARG ||
	    (sn && mackie_host_connection_reply_encode(NULL, sn, resp, rn, model, &out) != SPEC_E_ARG) ||
	    (rn && mackie_host_connection_reply_encode(serial, sn, NULL, rn, model, &out) != SPEC_E_ARG))
		abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	struct fuzz_in in;
	struct fuzz_map fm;
	uint8_t model;

	if (size >= FUZZ_INPUT_MAX)
		return -1;
	in = fuzz_in(data, size);
	fuzz_map_init(&fm, &in);
	model = fuzz_u8(&in);
	check_fixed(model);
	while (in.n > 0) {
		uint8_t sel = fuzz_u8(&in);
		unsigned a = fuzz_u32(&in);
		struct fuzz_num p = fuzz_num(&in);
		size_t n, m;
		uint8_t *bytes = fuzz_msg(&in, 40, &n);
		uint8_t *more = fuzz_msg(&in, 40, &m);
		size_t cn = n < 10 ? n : 10;
		unsigned *c = (unsigned *)fuzz_alloc(cn * sizeof *c);
		char *name = (char *)fuzz_alloc(n + 8);

		switch (sel % 8) {
		case 0: check_fader(a, p, fuzz_u32(&in)); break;
		case 1: check_led(a, sel >> 3); break;
		case 2: check_notes(&fm, a & 0xff, a >> 8); break;
		case 3: check_ring(&fm, a & 0xff, p, a >> 8 & 0xff, sel >> 3 & 1); break;
		case 4: check_meter(a, p, sel >> 3 & 7); break;
		case 5:
			for (size_t i = 0; i < cn; i++)
				c[i] = bytes[i] % 9;
			if (sel & 0x40 && n)
				strcpy(name, colour_names[bytes[0] % SPEC_COLOURS]);
			else {
				memcpy(name, bytes, n);
				name[n] = '\0';
			}
			check_colours(c, cn, name, model);
			break;
		case 6: check_reply(bytes, n, more, m, model); break;
		default: check_fixed((uint8_t)a); break;
		}
		free(name);
		free(c);
		free(more);
		free(bytes);
	}
	return 0;
}

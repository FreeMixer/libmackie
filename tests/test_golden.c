// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The byte goldens: every C encoder against the bytes the TypeScript encoder it was ported
 * from sent for the same input, and every C decoder against what the TypeScript decoder read
 * from the same bytes. tests/golden/mcu_golden.h is generated from the TypeScript module by
 * tools/gen-goldens.mjs and committed; nothing here needs Node.
 *
 *  1. THE MAP AND THE CONSTANTS. The map the TypeScript resolves from the X-Touch profile
 *     passes mackie_map_check, and every exported wire constant has the C value.
 *  2. ENCODERS, BYTE FOR BYTE: fader (position and 14-bit), button LED, V-Pot ring, meter,
 *     scribble, strip scribble, scribble backlights (refusals included), reset, device query,
 *     version request, host-connection reply, handshake response, timecode and assignment
 *     displays, the seven-segment code of every code point, strip and touch notes.
 *  3. DECODERS: button, fader, V-Pot, jog, handshake, version reply, each including the
 *     inputs the TypeScript refused.
 *  4. BACK AGAIN. What the encoders send decodes to what they were given: every 14-bit fader
 *     value, every LED note through the map, every ring and meter byte, the host-connection
 *     reply through the handshake decoder.
 *  5. THE HANDSHAKE GROUND TRUTH, captured live on an X-Touch, as the TypeScript passed it.
 */
#include "check.h"
#include "golden/mcu_golden.h"

#define N(a) (sizeof (a) / sizeof (a)[0])

static void map_and_constants(void)
{
	CHK(mackie_map_check(&golden_map) == MACKIE_OK);

	CHK(GOLDEN_NOTE_ON == MACKIE_NOTE_ON);
	CHK(GOLDEN_NOTE_OFF == MACKIE_NOTE_OFF);
	CHK(GOLDEN_CONTROL_CHANGE == MACKIE_CONTROL_CHANGE);
	CHK(GOLDEN_PITCH_BEND == MACKIE_PITCH_BEND);
	CHK(GOLDEN_CHANNEL_PRESSURE == MACKIE_CHANNEL_PRESSURE);
	CHK(GOLDEN_SYSEX_START == MACKIE_SYSEX_START);
	CHK(GOLDEN_SYSEX_END == MACKIE_SYSEX_END);
	CHK(GOLDEN_LED_ON == MACKIE_LED_ON);
	CHK(GOLDEN_LED_FLASH == MACKIE_LED_FLASH);
	CHK(GOLDEN_LED_OFF == MACKIE_LED_OFF);
	CHK(GOLDEN_MCU_STRIPS == MACKIE_STRIPS);
	CHK(GOLDEN_PITCHBEND_MAX == MACKIE_PITCHBEND_MAX);
	CHK(GOLDEN_MCU_MANUFACTURER_0 == MACKIE_MANUFACTURER_0);
	CHK(GOLDEN_MCU_MANUFACTURER_1 == MACKIE_MANUFACTURER_1);
	CHK(GOLDEN_MCU_MANUFACTURER_2 == MACKIE_MANUFACTURER_2);
	CHK(GOLDEN_MCU_MODEL_MCU == MACKIE_MODEL_MCU);
	CHK(GOLDEN_LCD_ROW_CHARS == MACKIE_LCD_ROW_CHARS);
	CHK(GOLDEN_LCD_ROWS == MACKIE_LCD_ROWS);
	CHK(GOLDEN_METER_SEGMENTS == MACKIE_METER_SEGMENTS);
	CHK(GOLDEN_HS_DEVICE_QUERY == MACKIE_HS_DEVICE_QUERY);
	CHK(GOLDEN_HS_HOST_CONNECTION_QUERY == MACKIE_HS_HOST_CONNECTION_QUERY);
	CHK(GOLDEN_HS_HOST_CONNECTION_REPLY == MACKIE_HS_HOST_CONNECTION_REPLY);
	CHK(GOLDEN_HS_CONNECTION_CONFIRM == MACKIE_HS_CONNECTION_CONFIRM);
	CHK(GOLDEN_HS_CONNECTION_ERROR == MACKIE_HS_CONNECTION_ERROR);
	CHK(GOLDEN_HS_VERSION_REQUEST == MACKIE_HS_VERSION_REQUEST);
	CHK(GOLDEN_HS_VERSION_REPLY == MACKIE_HS_VERSION_REPLY);

	/* the note bases and display CCs the TypeScript derived from the map, derived here */
	CHK(GOLDEN_NOTE_REC_BASE == golden_map.strip_note[MACKIE_STRIP_REC][0]);
	CHK(GOLDEN_NOTE_SOLO_BASE == golden_map.strip_note[MACKIE_STRIP_SOLO][0]);
	CHK(GOLDEN_NOTE_MUTE_BASE == golden_map.strip_note[MACKIE_STRIP_MUTE][0]);
	CHK(GOLDEN_NOTE_SELECT_BASE == golden_map.strip_note[MACKIE_STRIP_SELECT][0]);
	CHK(GOLDEN_NOTE_VPOT_PUSH_BASE == golden_map.strip_note[MACKIE_STRIP_VPOT_PUSH][0]);
	CHK(GOLDEN_NOTE_FADER_TOUCH_BASE == golden_map.touch_note[0]);
	CHK(GOLDEN_NOTE_FADER_TOUCH_MASTER == golden_map.touch_note[MACKIE_STRIPS]);
	CHK(GOLDEN_CC_VPOT_BASE == golden_map.vpot_cc[0]);
	CHK(GOLDEN_CC_VPOT_RING_BASE == golden_map.ring_cc[0]);
	CHK(GOLDEN_CC_ASSIGNMENT_RIGHT == golden_map.assignment_cc);
	CHK(GOLDEN_CC_ASSIGNMENT_LEFT == golden_map.assignment_cc + 1);
	CHK(GOLDEN_CC_TIMECODE_RIGHTMOST == golden_map.timecode_cc);
	CHK(GOLDEN_TIMECODE_DIGITS == golden_map.timecode_digits);
	CHK(GOLDEN_NOTE_SHIFT == golden_map.global_note[MACKIE_G_SHIFT]);
	CHK(GOLDEN_NOTE_PLAY == golden_map.global_note[MACKIE_G_PLAY]);

	CHK(N(golden_colours) == MACKIE_SCRIBBLE_COLOURS);
	for (size_t i = 0; i < N(golden_colours); i++) {
		CHK(strcmp(golden_colours[i].name, mackie_scribble_colours[i].name) == 0);
		CHK(memcmp(golden_colours[i].rgb, mackie_scribble_colours[i].rgb, 3) == 0);
		CHK(mackie_scribble_colour_index(golden_colours[i].name) == (int)i);
	}
	CHK(strcmp(GOLDEN_SCRIBBLE_COLOUR_DEFAULT, MACKIE_SCRIBBLE_COLOUR_DEFAULT) == 0);
	CHK(strcmp(GOLDEN_SCRIBBLE_COLOUR_EMPTY, MACKIE_SCRIBBLE_COLOUR_EMPTY) == 0);
}

static void encoders(void)
{
	struct mackie_msg m, d[MACKIE_DISPLAY_MAX_DIGITS];
	char what[160];

	for (size_t i = 0; i < N(golden_fader); i++) {
		snprintf(what, sizeof what, "fader[%zu] ch %u pos %.17g", i, golden_fader[i].channel, golden_fader[i].position);
		CHK(mackie_fader_encode(golden_fader[i].channel, golden_fader[i].position, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_fader[i].bytes, 3);
	}
	for (size_t i = 0; i < N(golden_fader14); i++) {
		snprintf(what, sizeof what, "fader14[%zu] value %u", i, golden_fader14[i].value);
		CHK(mackie_fader_encode14(golden_fader14[i].channel, golden_fader14[i].value, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_fader14[i].bytes, 3);
	}
	for (size_t i = 0; i < N(golden_led); i++) {
		snprintf(what, sizeof what, "led[%zu] note %u", i, golden_led[i].note);
		CHK(mackie_button_led_encode(golden_led[i].note, golden_led[i].state, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_led[i].bytes, 3);
	}
	for (size_t i = 0; i < N(golden_ring); i++) {
		snprintf(what, sizeof what, "ring[%zu] encoder %u pos %g", i, golden_ring[i].encoder, golden_ring[i].position);
		CHK(mackie_ring_encode(&golden_map, golden_ring[i].encoder, golden_ring[i].position,
				       golden_ring[i].mode, golden_ring[i].center, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_ring[i].bytes, 3);
	}
	CHK(GOLDEN_RING_PAST_LAST_REFUSED == 1);
	CHK(mackie_ring_encode(&golden_map, MACKIE_STRIPS, 1, MACKIE_RING_SINGLE, 0, &m) == MACKIE_E_RANGE);
	for (size_t i = 0; i < N(golden_meter); i++) {
		snprintf(what, sizeof what, "meter[%zu] strip %u level %g", i, golden_meter[i].strip, golden_meter[i].level);
		CHK(mackie_meter_encode(golden_meter[i].strip, golden_meter[i].level, golden_meter[i].overload, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_meter[i].bytes, 2);
	}
	for (size_t i = 0; i < N(golden_scribble); i++) {
		snprintf(what, sizeof what, "scribble[%zu] offset %u \"%s\"", i, golden_scribble[i].offset, golden_scribble[i].text);
		CHK(mackie_scribble_encode(golden_scribble[i].offset, golden_scribble[i].text, golden_scribble[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_scribble[i].bytes, golden_scribble[i].len);
	}
	for (size_t i = 0; i < N(golden_strip_scribble); i++) {
		snprintf(what, sizeof what, "strip scribble[%zu] strip %u row %u \"%s\"", i, golden_strip_scribble[i].strip,
			 golden_strip_scribble[i].row, golden_strip_scribble[i].text);
		CHK(mackie_strip_scribble_encode(golden_strip_scribble[i].strip, golden_strip_scribble[i].row,
						 golden_strip_scribble[i].text, golden_strip_scribble[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_strip_scribble[i].bytes, golden_strip_scribble[i].len);
	}
	for (size_t i = 0; i < N(golden_colours_msg); i++) {
		snprintf(what, sizeof what, "scribble colours[%zu]", i);
		int rc = mackie_scribble_colours_encode(golden_colours_msg[i].colours, golden_colours_msg[i].n,
							golden_colours_msg[i].model, &m);
		if (golden_colours_msg[i].len == 0) {
			/* the TypeScript threw RangeError */
			CHK(rc == MACKIE_E_RANGE);
		} else {
			CHK(rc == MACKIE_OK);
			same_bytes(what, m.bytes, m.len, golden_colours_msg[i].bytes, golden_colours_msg[i].len);
		}
	}
	for (size_t i = 0; i < N(golden_model_msgs); i++) {
		snprintf(what, sizeof what, "reset / query / version request, model %02x", golden_model_msgs[i].model);
		CHK(mackie_reset_encode(golden_model_msgs[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_model_msgs[i].reset, 8);
		CHK(mackie_device_query_encode(golden_model_msgs[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_model_msgs[i].query, 7);
		CHK(mackie_version_request_encode(golden_model_msgs[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_model_msgs[i].version, 8);
	}
	for (size_t i = 0; i < N(golden_timecode); i++) {
		uint8_t flat[3 * MACKIE_DISPLAY_MAX_DIGITS];
		size_t n = 0;
		snprintf(what, sizeof what, "timecode[%zu] \"%s\"", i, golden_timecode[i].text);
		CHK(mackie_timecode_encode(&golden_map, golden_timecode[i].text, d, N(d), &n) == MACKIE_OK);
		CHK(n == golden_timecode[i].n);
		for (size_t k = 0; k < n && k < N(d); k++) {
			CHK(d[k].len == 3);
			memcpy(flat + 3 * k, d[k].bytes, 3);
		}
		same_bytes(what, flat, 3 * n, golden_timecode[i].bytes, 3 * golden_timecode[i].n);
	}
	for (size_t i = 0; i < N(golden_assignment); i++) {
		uint8_t flat[6];
		snprintf(what, sizeof what, "assignment[%zu] \"%s\"", i, golden_assignment[i].text);
		CHK(mackie_assignment_encode(&golden_map, golden_assignment[i].text, d) == MACKIE_OK);
		memcpy(flat, d[0].bytes, 3);
		memcpy(flat + 3, d[1].bytes, 3);
		same_bytes(what, flat, 6, golden_assignment[i].bytes, 6);
	}
	/* the seven-segment code of every code point: ASCII from its table, U+0080..U+10FFFF a blank
	 * unless the fold list names it */
	for (uint32_t c = 0; c < 128; c++)
		if (mackie_seven_segment_code(c) != golden_seg7_ascii[c]) {
			fprintf(stderr, "FAIL seg7 U+%04X: %02x, want %02x\n", (unsigned)c,
				(unsigned)mackie_seven_segment_code(c), golden_seg7_ascii[c]);
			fails++;
		}
	size_t f = 0;
	for (uint32_t c = 0x80; c <= 0x10ffff; c++) {
		if (c >= 0xd800 && c <= 0xdfff)
			continue;
		int want = 0x20;
		if (f < N(golden_seg7_folds) && golden_seg7_folds[f].cp == c)
			want = golden_seg7_folds[f++].code;
		if (mackie_seven_segment_code(c) != want) {
			fprintf(stderr, "FAIL seg7 U+%04X: %02x, want %02x\n", (unsigned)c,
				(unsigned)mackie_seven_segment_code(c), (unsigned)want);
			fails++;
		}
	}
	checks += 0x110000;
	CHK(f == N(golden_seg7_folds));
	CHK(mackie_seven_segment_code(0xd83d) == GOLDEN_SEG7_LONE_SURROGATE);

	for (size_t i = 0; i < N(golden_response); i++) {
		uint8_t r[4];
		snprintf(what, sizeof what, "handshake response[%zu]", i);
		mackie_handshake_response(golden_response[i].challenge, golden_response[i].n, r);
		same_bytes(what, r, 4, golden_response[i].response, 4);
	}
	for (size_t i = 0; i < N(golden_reply); i++) {
		snprintf(what, sizeof what, "host connection reply[%zu]", i);
		CHK(mackie_host_connection_reply_encode(golden_reply[i].serial, golden_reply[i].serial_len,
							golden_reply[i].response, golden_reply[i].response_len,
							golden_reply[i].model, &m) == MACKIE_OK);
		same_bytes(what, m.bytes, m.len, golden_reply[i].bytes, golden_reply[i].len);
	}
	for (size_t i = 0; i < N(golden_strip_note); i++) {
		uint8_t note = 0xff;
		int rc = mackie_strip_note(&golden_map, golden_strip_note[i].button, golden_strip_note[i].strip, &note);
		if (golden_strip_note[i].note < 0)
			CHK(rc == MACKIE_E_RANGE);
		else
			CHK(rc == MACKIE_OK && note == golden_strip_note[i].note);
	}
	for (size_t i = 0; i < N(golden_touch_note); i++) {
		uint8_t note = 0xff;
		CHK(mackie_touch_note(&golden_map, golden_touch_note[i].strip, &note) == MACKIE_OK);
		CHK(note == golden_touch_note[i].note);
	}
}

static void decoders(void)
{
	for (size_t i = 0; i < N(golden_button); i++) {
		struct mackie_button b = { .kind = 0 };
		int rc = mackie_button_decode(&golden_map, golden_button[i].msg, golden_button[i].len, &b);
		if (!golden_button[i].ok) {
			if (rc != MACKIE_E_NOT_MINE)
				fprintf(stderr, "  button[%zu] %02x %02x: decoded, the TypeScript did not\n", i,
					golden_button[i].msg[0], golden_button[i].msg[1]);
			CHK(rc == MACKIE_E_NOT_MINE);
			continue;
		}
		CHK(rc == MACKIE_OK);
		CHK(b.kind == golden_button[i].kind);
		CHK(b.strip == golden_button[i].strip);
		CHK(b.pressed == golden_button[i].pressed);
		if (golden_button[i].id)
			CHK(b.id && strcmp(b.id, golden_button[i].id) == 0);
		else
			CHK(b.id == NULL);
	}
	for (size_t i = 0; i < N(golden_fader_dec); i++) {
		struct mackie_fader fd;
		int rc = mackie_fader_decode(&golden_map, golden_fader_dec[i].msg, golden_fader_dec[i].len, &fd);
		if (!golden_fader_dec[i].ok) {
			CHK(rc == MACKIE_E_NOT_MINE);
			continue;
		}
		CHK(rc == MACKIE_OK);
		CHK(fd.channel == golden_fader_dec[i].channel);
		/* the same integer over the same divisor: equal to the last bit */
		CHK(fd.position == golden_fader_dec[i].position);
	}
	for (size_t i = 0; i < N(golden_cc_dec); i++) {
		struct mackie_vpot v;
		int delta = 0;
		int rc = mackie_vpot_decode(&golden_map, golden_cc_dec[i].msg, golden_cc_dec[i].len, &v);
		if (golden_cc_dec[i].vpot_ok)
			CHK(rc == MACKIE_OK && v.encoder == golden_cc_dec[i].encoder && v.delta == golden_cc_dec[i].vpot_delta);
		else
			CHK(rc == MACKIE_E_NOT_MINE);
		rc = mackie_jog_decode(&golden_map, golden_cc_dec[i].msg, golden_cc_dec[i].len, &delta);
		if (golden_cc_dec[i].jog_ok)
			CHK(rc == MACKIE_OK && delta == golden_cc_dec[i].jog_delta);
		else
			CHK(rc == MACKIE_E_NOT_MINE);
	}
	for (size_t i = 0; i < N(golden_handshake_dec); i++) {
		struct mackie_handshake h;
		char text[32];
		int rc = mackie_handshake_decode(golden_handshake_dec[i].msg, golden_handshake_dec[i].len,
						 golden_handshake_dec[i].model, &h);
		if (golden_handshake_dec[i].ok) {
			CHK(rc == MACKIE_OK);
			CHK(h.sub == golden_handshake_dec[i].sub);
			CHK(h.serial_len == golden_handshake_dec[i].serial_len);
			CHK(memcmp(h.serial, golden_handshake_dec[i].serial, h.serial_len) == 0);
			CHK(h.challenge_len == golden_handshake_dec[i].challenge_len);
			CHK(memcmp(h.challenge, golden_handshake_dec[i].challenge, h.challenge_len) == 0);
		} else {
			CHK(rc == MACKIE_E_NOT_MINE);
		}
		rc = mackie_version_reply_decode(golden_handshake_dec[i].msg, golden_handshake_dec[i].len,
						 golden_handshake_dec[i].model, text, sizeof text);
		if (golden_handshake_dec[i].version_ok) {
			CHK(rc == (int)golden_handshake_dec[i].version_len);
			CHK(memcmp(text, golden_handshake_dec[i].version, golden_handshake_dec[i].version_len) == 0);
		} else {
			CHK(rc == MACKIE_E_NOT_MINE);
		}
	}
}

/* What the encoders send decodes back to what they were given. */
static void back_again(void)
{
	struct mackie_msg m;
	struct mackie_fader fd;
	struct mackie_button b;
	struct mackie_handshake h;

	/* every 14-bit value on every fader channel the map declares */
	for (unsigned ch = 0; ch <= MACKIE_STRIPS; ch++)
		for (unsigned v = 0; v <= MACKIE_PITCHBEND_MAX; v++) {
			mackie_fader_encode14(ch, v, &m);
			if (mackie_fader_decode(&golden_map, m.bytes, m.len, &fd) != MACKIE_OK || fd.channel != ch ||
			    fd.value != v) {
				fprintf(stderr, "FAIL fader ch %u value %u does not come back\n", ch, v);
				fails++;
			}
			/* and the position the decoder reports encodes to the same bytes */
			struct mackie_msg again;
			mackie_fader_encode(ch, fd.position, &again);
			if (again.len != 3 || memcmp(again.bytes, m.bytes, 3)) {
				fprintf(stderr, "FAIL fader ch %u value %u: its position does not encode back\n", ch, v);
				fails++;
			}
		}
	checks += (MACKIE_STRIPS + 1) * (MACKIE_PITCHBEND_MAX + 1);

	/* a lit LED on a strip button is a press of that button on that strip */
	for (unsigned k = 0; k < MACKIE_STRIP_BUTTONS; k++)
		for (unsigned s = 0; s < MACKIE_STRIPS; s++) {
			mackie_button_led_encode(golden_map.strip_note[k][s], MACKIE_LED_STATE_ON, &m);
			CHK(mackie_button_decode(&golden_map, m.bytes, m.len, &b) == MACKIE_OK);
			CHK(b.kind == (enum mackie_button_kind)(MACKIE_BTN_REC + k) && b.strip == (int)s && b.pressed);
			mackie_button_led_encode(golden_map.strip_note[k][s], MACKIE_LED_STATE_OFF, &m);
			CHK(mackie_button_decode(&golden_map, m.bytes, m.len, &b) == MACKIE_OK && !b.pressed);
		}
	for (size_t k = 0; k < golden_map.n_keys; k++) {
		mackie_button_led_encode(golden_map.keys[k].note, MACKIE_LED_STATE_FLASH, &m);
		CHK(mackie_button_decode(&golden_map, m.bytes, m.len, &b) == MACKIE_OK);
		CHK(b.kind == MACKIE_BTN_KEY && b.id == golden_map.keys[k].id && b.pressed);
	}

	/* the ring byte carries position, mode and centre in their fields */
	for (unsigned e = 0; e < MACKIE_STRIPS; e++)
		for (int pos = 0; pos <= 11; pos++)
			for (int mode = 0; mode <= MACKIE_RING_SPREAD; mode++)
				for (int c = 0; c <= 1; c++) {
					mackie_ring_encode(&golden_map, e, pos, (enum mackie_ring_mode)mode, c, &m);
					CHK(m.bytes[1] == golden_map.ring_cc[e]);
					CHK((m.bytes[2] & 0x0f) == pos && ((m.bytes[2] >> 4) & 3) == mode &&
					    !!(m.bytes[2] & 0x40) == c);
				}

	/* the meter byte carries strip and level */
	for (unsigned s = 0; s < MACKIE_STRIPS; s++)
		for (int l = 0; l <= MACKIE_METER_SEGMENTS; l++) {
			mackie_meter_encode(s, l, MACKIE_OVERLOAD_NONE, &m);
			CHK(m.bytes[1] >> 4 == s && (m.bytes[1] & 0x0f) == l);
		}

	/* the host-connection reply reads back through the handshake decoder */
	for (size_t i = 0; i < N(golden_reply); i++) {
		if (golden_reply[i].serial_len < MACKIE_SERIAL_LEN || golden_reply[i].response_len < MACKIE_CHALLENGE_LEN)
			continue;
		mackie_host_connection_reply_encode(golden_reply[i].serial, golden_reply[i].serial_len, golden_reply[i].response,
						    golden_reply[i].response_len, golden_reply[i].model, &m);
		CHK(mackie_handshake_decode(m.bytes, m.len, golden_reply[i].model, &h) == MACKIE_OK);
		CHK(h.sub == MACKIE_HS_HOST_CONNECTION_REPLY);
		CHK(h.serial_len == MACKIE_SERIAL_LEN && memcmp(h.serial, golden_reply[i].serial, MACKIE_SERIAL_LEN) == 0);
		CHK(h.challenge_len == MACKIE_CHALLENGE_LEN &&
		    memcmp(h.challenge, golden_reply[i].response, MACKIE_CHALLENGE_LEN) == 0);
	}
}

/* The exchange captured on an X-Touch (fw V1.22, 2026-07-03), as the TypeScript passed it. */
static void ground_truth(void)
{
	struct mackie_msg m;
	struct mackie_handshake h;
	uint8_t r[4];
	char text[16];

	CHK(mackie_device_query_encode(MACKIE_MODEL_MCU, &m) == MACKIE_OK);
	same_bytes("device query", m.bytes, m.len, golden_device_query, sizeof golden_device_query);
	CHK(mackie_handshake_decode(golden_observed_query, sizeof golden_observed_query, MACKIE_MODEL_MCU, &h) == MACKIE_OK);
	CHK(h.sub == MACKIE_HS_HOST_CONNECTION_QUERY);
	mackie_handshake_response(h.challenge, h.challenge_len, r);
	same_bytes("response to the observed challenge", r, 4, golden_observed_response, 4);
	CHK(mackie_host_connection_reply_encode(h.serial, h.serial_len, r, 4, MACKIE_MODEL_MCU, &m) == MACKIE_OK);
	same_bytes("host connection reply", m.bytes, m.len, golden_host_connection_reply, sizeof golden_host_connection_reply);
	CHK(mackie_version_request_encode(MACKIE_MODEL_MCU, &m) == MACKIE_OK);
	same_bytes("version request", m.bytes, m.len, golden_version_request, sizeof golden_version_request);
	CHK(mackie_version_reply_decode(golden_observed_version_reply, sizeof golden_observed_version_reply,
					MACKIE_MODEL_MCU, text, sizeof text) == (int)strlen(GOLDEN_OBSERVED_VERSION));
	CHK(strcmp(text, GOLDEN_OBSERVED_VERSION) == 0);
}

int main(void)
{
	map_and_constants();
	encoders();
	decoders();
	back_again();
	ground_truth();
	return verdict("test_golden");
}

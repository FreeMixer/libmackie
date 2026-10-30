// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The TypeScript MCU suite (mcu.test.ts), ported case for case: the same six describes, the
 * same names, the same bytes. The map is the X-Touch's, as the golden header carries it.
 *
 * Two of its cases test functions that did not move into this library, and are not here:
 * "maps unit + pan to ring positions" (ringPositionFromUnit / ringPositionFromPan become ring
 * rendering in the surface behaviour layer) and "maps a 0..1 value to 0..12 segments"
 * (meterSegments, already superseded by the host's own meter step). The scribble-backlight
 * describe of the scribble-colour round-trip suite (encodeScribbleColours) is ported at the
 * end, as the design maps it here too.
 *
 * Beyond the port: the refusals a C API adds (NULL, a short buffer, a map that names a note
 * twice), each with its own case at the end.
 */
#include "check.h"
#include "golden/mcu_golden.h"

#include <mackie/mackie_codec.h>

static const struct mackie_map *MAP = &golden_map;

#define NOTE_MUTE_BASE   (MAP->strip_note[MACKIE_STRIP_MUTE][0])
#define NOTE_SOLO_BASE   (MAP->strip_note[MACKIE_STRIP_SOLO][0])
#define NOTE_SELECT_BASE (MAP->strip_note[MACKIE_STRIP_SELECT][0])
#define NOTE_REC_BASE    (MAP->strip_note[MACKIE_STRIP_REC][0])
#define CC_VPOT_BASE     (MAP->vpot_cc[0])
#define CC_VPOT_RING_BASE (MAP->ring_cc[0])

static int button(const uint8_t *msg, size_t len, struct mackie_button *b)
{
	return mackie_button_decode(MAP, msg, len, b);
}
#define BUTTON(b, ...) button((const uint8_t[]){ __VA_ARGS__ }, sizeof((const uint8_t[]){ __VA_ARGS__ }), (b))

/* ==== describe('MCU faders (pitch bend, 14-bit)') ========================================== */
static void faders(void)
{
	struct mackie_msg m = { 0 };
	struct mackie_fader f = { 0 };

	/* encodes 0 and full scale to the pitch-bend extremes */
	mackie_fader_encode(0, 0, &m);
	MSG_IS("fader 0 at 0", m, MACKIE_PITCH_BEND | 0, 0, 0);
	mackie_fader_encode(0, 1, &m);
	MSG_IS("fader 0 at 1", m, MACKIE_PITCH_BEND | 0, 0x7f, 0x7f);

	/* puts the fader index in the status low nibble */
	mackie_fader_encode(5, 0, &m);
	CHK(m.bytes[0] == (MACKIE_PITCH_BEND | 5));
	mackie_fader_encode(8, 0, &m);
	CHK(m.bytes[0] == (MACKIE_PITCH_BEND | 8)); /* master */

	/* round-trips a mid position through decode within one LSB */
	mackie_fader_encode(3, 0.5, &m);
	CHK(mackie_fader_decode(MAP, m.bytes, m.len, &f) == MACKIE_OK);
	CHK(f.channel == 3);
	CHK(f.position > 0.5 - 0.0005 && f.position < 0.5 + 0.0005);

	/* decodes the 14-bit value from lsb+msb */
	CHK(mackie_fader_decode(MAP, (const uint8_t[]){ MACKIE_PITCH_BEND | 2, 0x7f, 0x7f }, 3, &f) == MACKIE_OK);
	CHK(f.position == (double)MACKIE_PITCHBEND_MAX / MACKIE_PITCHBEND_MAX);

	/* returns null for a non-pitch-bend message */
	CHK(mackie_fader_decode(MAP, (const uint8_t[]){ MACKIE_NOTE_ON, 10, 20 }, 3, &f) == MACKIE_E_NOT_MINE);
}

/* ==== describe('MCU buttons (notes)') ====================================================== */
static void buttons(void)
{
	struct mackie_button b = { 0 };
	struct mackie_msg m = { 0 };
	uint8_t note;

	/* maps per-strip note bases to kind + strip */
	CHK(BUTTON(&b, MACKIE_NOTE_ON, NOTE_MUTE_BASE + 2, 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_MUTE && b.strip == 2 && b.pressed && !b.id);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, NOTE_SOLO_BASE + 0, 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_SOLO && b.strip == 0 && b.pressed);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, NOTE_SELECT_BASE + 7, 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_SELECT && b.strip == 7 && b.pressed);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, NOTE_REC_BASE + 4, 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_REC && b.strip == 4 && b.pressed);

	/* treats Note On velocity 0 and Note Off as release */
	CHK(BUTTON(&b, MACKIE_NOTE_ON, NOTE_MUTE_BASE, 0) == MACKIE_OK && !b.pressed);
	CHK(BUTTON(&b, 0x80, NOTE_MUTE_BASE, 64) == MACKIE_OK && !b.pressed);

	/* decodes global transport + nav notes with no strip */
	CHK(BUTTON(&b, MACKIE_NOTE_ON, MAP->global_note[MACKIE_G_PLAY], 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_PLAY && b.strip == -1 && b.pressed);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, MAP->global_note[MACKIE_G_BANK_RIGHT], 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_BANK_RIGHT && b.strip == -1 && b.pressed);

	/* decodes fader-touch, including the master sensor */
	mackie_touch_note(MAP, 1, &note);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, note, 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_FADER_TOUCH && b.strip == 1 && b.pressed);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, MAP->touch_note[MACKIE_STRIPS], 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_FADER_TOUCH);

	/* decodes the modifier row: SHIFT is note 70 (0x46) */
	CHK(MAP->global_note[MACKIE_G_SHIFT] == 70);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, MAP->global_note[MACKIE_G_SHIFT], 127) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_SHIFT && b.strip == -1 && b.pressed);
	CHK(BUTTON(&b, MACKIE_NOTE_ON, MAP->global_note[MACKIE_G_SHIFT], 0) == MACKIE_OK);
	CHK(b.kind == MACKIE_BTN_SHIFT && !b.pressed);
	/* distinct from the master fader-touch sensor (0x70 = 112 decimal): no note collision */
	CHK(MAP->global_note[MACKIE_G_SHIFT] != MAP->touch_note[MACKIE_STRIPS]);

	/* encodes button LED states (the X-Touch profile puts strip 1 MUTE on note 16) */
	CHK(NOTE_MUTE_BASE == 16);
	mackie_button_led_encode(NOTE_MUTE_BASE, MACKIE_LED_STATE_ON, &m);
	MSG_IS("mute LED on", m, MACKIE_NOTE_ON, 16, MACKIE_LED_ON);
	mackie_button_led_encode(NOTE_MUTE_BASE, MACKIE_LED_STATE_OFF, &m);
	MSG_IS("mute LED off", m, MACKIE_NOTE_ON, 16, MACKIE_LED_OFF);
	mackie_button_led_encode(NOTE_MUTE_BASE, MACKIE_LED_STATE_FLASH, &m);
	MSG_IS("mute LED flash", m, MACKIE_NOTE_ON, 16, MACKIE_LED_FLASH);

	/* computes stripButtonNote from the base + index */
	CHK(mackie_strip_note(MAP, MACKIE_STRIP_MUTE, 3, &note) == MACKIE_OK && note == NOTE_MUTE_BASE + 3);
	CHK(mackie_strip_note(MAP, MACKIE_STRIP_REC, 0, &note) == MACKIE_OK);
}

/* ==== describe('MCU V-Pots (encoders)') ==================================================== */
static void vpots(void)
{
	struct mackie_vpot v = { 0 };
	struct mackie_msg m = { 0 };

	/* decodes clockwise + counter-clockwise relative ticks */
	CHK(mackie_vpot_decode(MAP, (const uint8_t[]){ MACKIE_CONTROL_CHANGE, CC_VPOT_BASE + 1, 0x01 }, 3, &v) == MACKIE_OK);
	CHK(v.encoder == 1 && v.delta == 1);
	CHK(mackie_vpot_decode(MAP, (const uint8_t[]){ MACKIE_CONTROL_CHANGE, CC_VPOT_BASE + 1, 0x41 }, 3, &v) == MACKIE_OK);
	CHK(v.encoder == 1 && v.delta == -1);
	CHK(mackie_vpot_decode(MAP, (const uint8_t[]){ MACKIE_CONTROL_CHANGE, CC_VPOT_BASE + 0, 0x03 }, 3, &v) == MACKIE_OK);
	CHK(v.encoder == 0 && v.delta == 3);

	/* ignores non-V-Pot CCs */
	CHK(mackie_vpot_decode(MAP, (const uint8_t[]){ MACKIE_CONTROL_CHANGE, 0x60, 1 }, 3, &v) == MACKIE_E_NOT_MINE);

	/* encodes an LED ring on the right output CC with mode + centre bits */
	CHK(mackie_ring_encode(MAP, 2, 6, MACKIE_RING_BOOST_CUT, 1, &m) == MACKIE_OK);
	CHK(m.bytes[0] == MACKIE_CONTROL_CHANGE);
	CHK(m.bytes[1] == CC_VPOT_RING_BASE + 2);
	CHK((m.bytes[2] & 0x0f) == 6);         /* position */
	CHK(((m.bytes[2] >> 4) & 0x03) == 1);  /* boostCut mode */
	CHK((m.bytes[2] & 0x40) == 0x40);      /* centre LED */

	/* clamps ring position into 0..11 */
	mackie_ring_encode(MAP, 0, 99, MACKIE_RING_SINGLE, 0, &m);
	CHK((m.bytes[2] & 0x0f) == 11);
	mackie_ring_encode(MAP, 0, -5, MACKIE_RING_SINGLE, 0, &m);
	CHK((m.bytes[2] & 0x0f) == 0);
}

/* ==== describe('MCU meters (channel pressure)') ============================================ */
static void meters(void)
{
	struct mackie_msg m = { 0 };

	/* packs strip into the high nibble and level into the low */
	mackie_meter_encode(3, 5, MACKIE_OVERLOAD_NONE, &m);
	MSG_IS("meter 3 at 5", m, MACKIE_CHANNEL_PRESSURE, (3 << 4) | 5);

	/* clamps the level to the segment count */
	mackie_meter_encode(0, 99, MACKIE_OVERLOAD_NONE, &m);
	CHK((m.bytes[1] & 0x0f) == MACKIE_METER_SEGMENTS);

	/* sets and clears the overload nibble */
	mackie_meter_encode(1, 0, MACKIE_OVERLOAD_SET, &m);
	CHK((m.bytes[1] & 0x0f) == 0x0e);
	mackie_meter_encode(1, 0, MACKIE_OVERLOAD_CLEAR, &m);
	CHK((m.bytes[1] & 0x0f) == 0x0f);
}

/* ==== describe('MCU scribble strips (SysEx)') ============================================== */
static void scribbles(void)
{
	struct mackie_msg m = { 0 };

	/* frames a write with the manufacturer + model + sub-command */
	mackie_scribble_encode(0, "AB", MACKIE_MODEL_MCU, &m);
	CHK(m.bytes[0] == MACKIE_SYSEX_START);
	CHK(m.bytes[1] == MACKIE_MANUFACTURER_0 && m.bytes[2] == MACKIE_MANUFACTURER_1 && m.bytes[3] == MACKIE_MANUFACTURER_2);
	CHK(m.bytes[4] == MACKIE_MODEL_MCU);
	CHK(m.bytes[5] == 0x12);
	CHK(m.bytes[6] == 0); /* offset */
	CHK(m.bytes[7] == 0x41 && m.bytes[8] == 0x42); /* 'A','B' */
	CHK(m.bytes[m.len - 1] == MACKIE_SYSEX_END);

	/* coerces non-ASCII to space and clamps to the LCD length */
	mackie_scribble_encode(0, "\xc3\xa9", MACKIE_MODEL_MCU, &m); /* é */
	CHK(m.bytes[7] == 0x20);
	mackie_scribble_encode(MACKIE_LCD_ROW_CHARS * 2 - 2, "ABCDEFGH", MACKIE_MODEL_MCU, &m); /* only 2 cells left */
	CHK(m.len == 7 + 2 + 1); /* header(7) + 2 chars + F7 */

	/* places a strip cell on the correct row/offset and pads to 7 */
	mackie_strip_scribble_encode(1, 1, "Hi", MACKIE_MODEL_MCU, &m);
	CHK(m.bytes[6] == MACKIE_LCD_ROW_CHARS + 7); /* row 1, strip 1 */
	CHK(m.len - 8 == 7);                         /* padded */

	/* encodes a reset */
	mackie_reset_encode(MACKIE_MODEL_MCU, &m);
	CHK(m.bytes[5] == 0x08);
	CHK(m.bytes[m.len - 1] == MACKIE_SYSEX_END);
}

/* ==== describe('MCU handshake: ground truth captured live (X-Touch fw V1.22, 2026-07-03)') == */
/* The device's reply to our device query, byte for byte as captured over ALSA seq: serial
 * "015640B" + challenge "0B8E" (both ASCII on this unit). */
static const uint8_t OBSERVED_CONNECTION_QUERY[] = {
	0xf0, 0x00, 0x00, 0x66, 0x14, 0x01,
	0x30, 0x31, 0x35, 0x36, 0x34, 0x30, 0x42, /* "015640B" */
	0x30, 0x42, 0x38, 0x45,                   /* "0B8E" */
	0xf7,
};

static void handshake(void)
{
	struct mackie_msg m = { 0 };
	struct mackie_handshake hs = { 0 };
	uint8_t r[4];
	char text[16];

	/* encodes the device query exactly as sent to the live unit */
	mackie_device_query_encode(MACKIE_MODEL_MCU, &m);
	MSG_IS("device query", m, 0xf0, 0x00, 0x00, 0x66, 0x14, 0x00, 0xf7);

	/* splits serial and challenge out of the observed connection query */
	CHK(mackie_handshake_decode(OBSERVED_CONNECTION_QUERY, sizeof OBSERVED_CONNECTION_QUERY, MACKIE_MODEL_MCU, &hs) == MACKIE_OK);
	CHK(hs.sub == MACKIE_HS_HOST_CONNECTION_QUERY);
	CHK(hs.serial_len == 7 && memcmp(hs.serial, "015640B", 7) == 0);
	CHK(hs.challenge_len == 4 && memcmp(hs.challenge, "0B8E", 4) == 0);

	/* computes the response sent (and silently accepted) live for challenge "0B8E" */
	mackie_handshake_response((const uint8_t[]){ 0x30, 0x42, 0x38, 0x45 }, 4, r);
	same_bytes("response to 0B8E", r, 4, (const uint8_t[]){ 0x33, 0x76, 0x17, 0x2a }, 4);

	/* encodes the host-connection reply byte-for-byte as sent to the live unit */
	mackie_handshake_response(hs.challenge, hs.challenge_len, r);
	mackie_host_connection_reply_encode(hs.serial, hs.serial_len, r, 4, MACKIE_MODEL_MCU, &m);
	MSG_IS("host connection reply", m,
	       0xf0, 0x00, 0x00, 0x66, 0x14, 0x02,
	       0x30, 0x31, 0x35, 0x36, 0x34, 0x30, 0x42,
	       0x33, 0x76, 0x17, 0x2a,
	       0xf7);

	/* computes a stable 7-bit-clean response for any challenge */
	mackie_handshake_response((const uint8_t[]){ 0x11, 0x22, 0x33, 0x44 }, 4, r);
	for (int i = 0; i < 4; i++)
		CHK(r[i] <= 0x7f);

	/* encodes the version request and decodes the observed "V1.22" reply */
	mackie_version_request_encode(MACKIE_MODEL_MCU, &m);
	MSG_IS("version request", m, 0xf0, 0x00, 0x00, 0x66, 0x14, 0x13, 0x00, 0xf7);
	/* observed reply, byte for byte */
	CHK(mackie_version_reply_decode((const uint8_t[]){ 0xf0, 0x00, 0x00, 0x66, 0x14, 0x14, 0x56, 0x31, 0x2e, 0x32, 0x32, 0xf7 },
					12, MACKIE_MODEL_MCU, text, sizeof text) == 5);
	CHK(strcmp(text, "V1.22") == 0);
	/* not a version reply: null */
	CHK(mackie_version_reply_decode(OBSERVED_CONNECTION_QUERY, sizeof OBSERVED_CONNECTION_QUERY, MACKIE_MODEL_MCU,
					text, sizeof text) == MACKIE_E_NOT_MINE);

	/* returns null for a non-handshake / wrong-manufacturer / wrong-model SysEx */
	CHK(mackie_handshake_decode((const uint8_t[]){ MACKIE_NOTE_ON, 1, 2 }, 3, MACKIE_MODEL_MCU, &hs) == MACKIE_E_NOT_MINE);
	CHK(mackie_handshake_decode((const uint8_t[]){ MACKIE_SYSEX_START, 0x7d, 0, 0, MACKIE_MODEL_MCU, 0, MACKIE_SYSEX_END }, 7,
				    MACKIE_MODEL_MCU, &hs) == MACKIE_E_NOT_MINE);
	/* the live unit ignored a query for model 0x10 (Logic Control); the decoder is as strict */
	CHK(mackie_handshake_decode((const uint8_t[]){ MACKIE_SYSEX_START, 0x00, 0x00, 0x66, 0x10, 0x00, MACKIE_SYSEX_END }, 7,
				    MACKIE_MODEL_MCU, &hs) == MACKIE_E_NOT_MINE);
}

/* ==== describe('encodeScribbleColours: the X-Touch backlight SysEx') ======================= */
static void scribble_colours(void)
{
	static const char *const names[] = { "off", "red", "green", "yellow", "blue", "magenta", "cyan", "white" };
	struct mackie_msg m = { 0 };

	/* the eight surface colours, in wire order */
	for (int i = 0; i < MACKIE_SCRIBBLE_COLOURS; i++)
		CHK(strcmp(mackie_scribble_colours[i].name, names[i]) == 0);

	/* carries one byte per strip, each the colour index, in strip order */
	for (unsigned i = 0; i < MACKIE_SCRIBBLE_COLOURS; i++) {
		unsigned c[8] = { 0 };
		uint8_t want[15] = { 0xf0, 0x00, 0x00, 0x66, 0x14, 0x72, 0, 0, 0, 0, 0, 0, 0, 0, 0xf7 };
		c[i] = i;
		want[6 + i] = (uint8_t)i;
		CHK(mackie_scribble_colours_encode(c, 8, MACKIE_MODEL_MCU, &m) == MACKIE_OK);
		same_bytes(names[i], m.bytes, m.len, want, sizeof want);
	}
	CHK(mackie_scribble_colours_encode((const unsigned[]){ 1, 2, 3, 4, 5, 6, 7, 0 }, 8, 0x15, &m) == MACKIE_OK);
	MSG_IS("colours on model 0x15", m, 0xf0, 0x00, 0x00, 0x66, 0x15, 0x72, 1, 2, 3, 4, 5, 6, 7, 0, 0xf7);

	/* refuses a message that is not eight valid colours */
	CHK(mackie_scribble_colours_encode((const unsigned[]){ 1, 2, 3 }, 3, MACKIE_MODEL_MCU, &m) == MACKIE_E_RANGE);
	CHK(mackie_scribble_colours_encode((const unsigned[]){ 1, 2, 3, 4, 5, 6, 7, 8 }, 8, MACKIE_MODEL_MCU, &m) == MACKIE_E_RANGE);
}

/* ==== the C API's own refusals ============================================================= */
static void refusals(void)
{
	struct mackie_msg m, d[4];
	struct mackie_button b = { 0 };
	struct mackie_map twice = golden_map;
	size_t n = 0;
	char text[4];
	uint8_t note;

	/* NULL is refused, never dereferenced */
	CHK(mackie_fader_encode(0, 0.5, NULL) == MACKIE_E_ARG);
	CHK(mackie_scribble_encode(0, NULL, MACKIE_MODEL_MCU, &m) == MACKIE_E_ARG);
	CHK(mackie_button_decode(NULL, (const uint8_t[]){ 0x90, 0, 127 }, 3, &b) == MACKIE_E_ARG);
	CHK(mackie_map_check(NULL) == MACKIE_E_ARG);
	CHK(mackie_strip_note(MAP, MACKIE_STRIP_MUTE, 8, &note) == MACKIE_E_RANGE);

	/* a value the wire cannot carry */
	CHK(mackie_fader_encode14(0, MACKIE_PITCHBEND_MAX + 1, &m) == MACKIE_E_RANGE);
	CHK(mackie_strip_scribble_encode(0, 2, "x", MACKIE_MODEL_MCU, &m) == MACKIE_E_RANGE);
	CHK(mackie_ring_encode(MAP, 0, 1, (enum mackie_ring_mode)4, 0, &m) == MACKIE_E_ARG);

	/* a short output is refused, not overrun */
	CHK(mackie_timecode_encode(MAP, "0123456789", d, 4, &n) == MACKIE_E_SPACE);
	CHK(mackie_version_reply_decode((const uint8_t[]){ 0xf0, 0x00, 0x00, 0x66, 0x14, 0x14, 0x56, 0x31, 0x2e, 0x32, 0x32, 0xf7 },
					12, MACKIE_MODEL_MCU, text, sizeof text) == MACKIE_E_SPACE);

	/* a map that names one note twice is refused whole */
	twice.global_note[MACKIE_G_PLAY] = twice.global_note[MACKIE_G_STOP];
	CHK(mackie_map_check(&twice) == MACKIE_E_RANGE);
	twice = golden_map;
	twice.timecode_cc = 0x7f; /* ten digits from 0x7f run past 7 bits */
	CHK(mackie_map_check(&twice) == MACKIE_E_RANGE);

	/* every code has a name */
	for (int c = MACKIE_OK; c >= MACKIE_E_NOT_MINE; c--)
		CHK(strcmp(mackie_strerror(c), "unknown error") != 0);
}

int main(void)
{
	faders();
	buttons();
	vpots();
	meters();
	scribbles();
	handshake();
	scribble_colours();
	refusals();
	return verdict("test_mcu");
}

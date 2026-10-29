/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * libmackie: the encoders and decoders. Pure functions over caller memory: no port, no
 * clock, no allocation.
 *
 * Every encoder writes one struct mackie_msg (or, for a display, one per digit) and returns
 * MACKIE_OK, or a negative code with *out untouched. Every decoder returns MACKIE_OK and
 * fills *out, MACKIE_E_NOT_MINE for a message of another kind (out untouched), or
 * MACKIE_E_ARG for a NULL pointer.
 *
 * Text arguments are NUL-terminated UTF-8. A surface cell holds one character, so text is
 * read as Unicode characters, never as bytes: "é" is one cell (shown as a blank), not two.
 * An invalid UTF-8 byte reads as U+FFFD, which shows as a blank. Where a width is counted
 * (a 7-character strip cell, a display's digits) it is counted in UTF-16 code units, as the
 * TypeScript the codec was ported from counts it: a character outside the Basic
 * Multilingual Plane takes two of them, and one cut in half shows as a blank.
 */
#ifndef MACKIE_CODEC_H
#define MACKIE_CODEC_H

#include <mackie/mackie.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==== Fader (pitch bend, 14-bit) ========================================================== */

/* A motor-fader position (0..1, clamped; NaN reads as 0) as a Pitch Bend message on
 * channel (0..7 = strips 1..8, 8 = master; only the low nibble is sent). LSB carries the
 * low 7 bits, MSB the high 7. The position is rounded to the nearest of 16384 steps, a half
 * step rounding up. */
int mackie_fader_encode(unsigned channel, double position, struct mackie_msg *out);
/* The same with the 14-bit value given: MACKIE_E_RANGE above MACKIE_PITCHBEND_MAX. */
int mackie_fader_encode14(unsigned channel, unsigned value, struct mackie_msg *out);

/* A decoded fader move from the surface (a touched, moving physical fader). */
struct mackie_fader {
	uint8_t channel;  /* 0..7 strip, 8 master */
	uint16_t value;   /* (msb << 7) | lsb */
	double position;  /* value / MACKIE_PITCHBEND_MAX */
};

/* A Pitch Bend on a channel the map declares a fader. A missing data byte reads as 0. */
int mackie_fader_decode(const struct mackie_map *m, const uint8_t *msg, size_t len,
			struct mackie_fader *out);

/* ==== Buttons (notes): SEL / MUTE / SOLO / REC / V-Pot push / transport / nav ============ */

enum mackie_button_kind {
	MACKIE_BTN_REC = 1,
	MACKIE_BTN_SOLO,
	MACKIE_BTN_MUTE,
	MACKIE_BTN_SELECT,
	MACKIE_BTN_VPOT_PUSH,
	MACKIE_BTN_FADER_TOUCH,
	/* the globals, in enum mackie_global order: MACKIE_BTN_BANK_LEFT + g */
	MACKIE_BTN_BANK_LEFT,
	MACKIE_BTN_BANK_RIGHT,
	MACKIE_BTN_CHANNEL_LEFT,
	MACKIE_BTN_CHANNEL_RIGHT,
	MACKIE_BTN_FLIP,
	MACKIE_BTN_MARKER,
	MACKIE_BTN_CYCLE,
	MACKIE_BTN_REWIND,
	MACKIE_BTN_FFWD,
	MACKIE_BTN_STOP,
	MACKIE_BTN_PLAY,
	MACKIE_BTN_RECORD,
	MACKIE_BTN_SHIFT,
	MACKIE_BTN_OPTION,
	MACKIE_BTN_CONTROL,
	MACKIE_BTN_ALT,
	/* any other key of the map: its id names it */
	MACKIE_BTN_KEY,
};

/* A decoded button event from the surface. */
struct mackie_button {
	enum mackie_button_kind kind;
	/* 0-based strip for per-strip buttons and fader-touch (the master's is MACKIE_STRIPS);
	 * -1 for a global button or a key */
	int8_t strip;
	/* 1 = pressed / touched, 0 = released */
	uint8_t pressed;
	/* for MACKIE_BTN_KEY: the map's id for it (the map's storage); NULL otherwise */
	const char *id;
};

/* A Note On/Off on a note the map names. Note Off, or Note On with velocity 0, both mean
 * "released"; a missing velocity reads as 0. */
int mackie_button_decode(const struct mackie_map *m, const uint8_t *msg, size_t len,
			 struct mackie_button *out);

enum mackie_led { MACKIE_LED_STATE_OFF = 0, MACKIE_LED_STATE_ON, MACKIE_LED_STATE_FLASH };

/* A button-LED state (host to surface): 90 note velocity. */
int mackie_button_led_encode(unsigned note, enum mackie_led state, struct mackie_msg *out);

/* The note of a per-strip button (strip 0..7): MACKIE_E_RANGE past the last strip. */
int mackie_strip_note(const struct mackie_map *m, enum mackie_strip_button button,
		      unsigned strip, uint8_t *note);
/* The fader-touch note of a strip (0..7) or the master (strip MACKIE_STRIPS, or anything
 * past it). */
int mackie_touch_note(const struct mackie_map *m, unsigned strip, uint8_t *note);

/* ==== V-Pots (rotary encoders): relative in, LED ring out ================================= */

/* A decoded V-Pot turn from the surface. */
struct mackie_vpot {
	uint8_t encoder; /* 0..7 */
	int16_t delta;   /* signed tick delta: +clockwise, -counter-clockwise */
};

/* A V-Pot CC (on any MIDI channel) that the map names. MCU relative "sign+magnitude": data
 * byte bit 6 (0x40) = direction (set = counter-clockwise), low 6 bits = tick count
 * (typically 1). */
int mackie_vpot_decode(const struct mackie_map *m, const uint8_t *msg, size_t len,
		       struct mackie_vpot *out);

/* The jog wheel's CC (the map's jog_cc) as a signed tick delta. Same relative sign+magnitude
 * as a V-Pot: bit 6 = counter-clockwise. MACKIE_E_NOT_MINE when the map has no jog. */
int mackie_jog_decode(const struct mackie_map *m, const uint8_t *msg, size_t len, int *delta);

/* V-Pot LED-ring display mode (how the 11 dots around a knob are lit). */
enum mackie_ring_mode {
	MACKIE_RING_SINGLE = 0,
	MACKIE_RING_BOOST_CUT = 1,
	MACKIE_RING_WRAP = 2,
	MACKIE_RING_SPREAD = 3,
};

/* A V-Pot LED-ring display (host to surface). position 0..11 (rounded, clamped; NaN reads as
 * 0) lights that many dots per mode; center lights the top centre LED (the "record"/detent
 * dot). Data byte (7 bits): bit 6 = centre LED, bits 5-4 = ring mode, bits 3-0 = position
 * (0 = all off, 11 = full). MACKIE_E_RANGE for an encoder past the last strip,
 * MACKIE_E_ARG for a mode outside the four. */
int mackie_ring_encode(const struct mackie_map *m, unsigned encoder, double position,
		       enum mackie_ring_mode mode, int center, struct mackie_msg *out);

/* ==== Meters (channel pressure) ============================================================ */

enum mackie_overload {
	MACKIE_OVERLOAD_NONE = 0, /* send the level */
	MACKIE_OVERLOAD_SET,      /* nibble 0xE: light the clip LED */
	MACKIE_OVERLOAD_CLEAR,    /* nibble 0xF: clear it */
};

/* A strip meter level (host to surface) as a Channel Pressure message. Data byte high nibble
 * = strip (0..7, low 3 bits sent), low nibble = level 0x0..0xC (0..12 LEDs; rounded and
 * clamped, NaN reads as 0). An overload other than NONE sends its nibble instead of the
 * level: the exact overload nibble is one of the values that wants live-hardware
 * confirmation. */
int mackie_meter_encode(unsigned strip, double level, enum mackie_overload overload,
			struct mackie_msg *out);

/* ==== LCD scribble strips (SysEx) =========================================================== */

/* A scribble-strip write: place text at offset (0..111; 0-55 = top row, 56-111 = bottom
 * row). A character outside 0x20..0x7E is sent as 0x20 (space), and the text is clamped so
 * it never runs past cell 111 (an offset past it writes no cell). The offset byte carries
 * its low 7 bits. */
int mackie_scribble_encode(unsigned offset, const char *text, uint8_t model,
			   struct mackie_msg *out);

/* A full 7-character scribble cell for strip (0..7) on row (0 = top, 1 = bottom). Each strip
 * owns a 7-character slice of its row; the text is padded or cut to 7. MACKIE_E_RANGE for a
 * row other than 0 or 1. */
int mackie_strip_scribble_encode(unsigned strip, unsigned row, const char *text, uint8_t model,
				 struct mackie_msg *out);

/* ---- Scribble backlight colour ---- */

#define MACKIE_SCRIBBLE_COLOURS 8

struct mackie_colour {
	const char *name;
	uint8_t rgb[3];
};

/* The X-Touch's scribble backlight colours, in wire order: a colour's INDEX here is its byte
 * in mackie_scribble_colours_encode(). Each carries the RGB it stands for, which is what a
 * host matches its strip palette against. The one table of them. */
extern const struct mackie_colour mackie_scribble_colours[MACKIE_SCRIBBLE_COLOURS];

/* The backlight of a strip with no colour, or a colour the palette does not carry. */
#define MACKIE_SCRIBBLE_COLOUR_DEFAULT "white"
/* The backlight of a strip with no channel on it. */
#define MACKIE_SCRIBBLE_COLOUR_EMPTY "off"

/* A surface colour's byte, by name; MACKIE_E_RANGE for a name the table does not carry. */
int mackie_scribble_colour_index(const char *name);

/* The eight scribble backlights: colours[n] is strip n's index in mackie_scribble_colours.
 * MACKIE_E_RANGE unless there are exactly eight and each is 0..7. */
int mackie_scribble_colours_encode(const unsigned *colours, size_t n, uint8_t model,
				   struct mackie_msg *out);

/* A device reset (clear LCD + all LEDs). */
int mackie_reset_encode(uint8_t model, struct mackie_msg *out);

/* ==== Seven-segment displays (CC) ============================================================ */

/* One character as an MCU 7-segment code: '@'/A-Z/'[\]^_' -> 0x00-0x1F, space..'?' ->
 * as-is. Lower case folds to upper (the segments draw 'B' as 'b' anyway); anything else is a
 * blank (0x20). A character whose upper case begins with one of those letters folds to it
 * as well ('ß' is "SS", the 'ﬁ' ligature "FI"), as the TypeScript's toUpperCase() did. */
int mackie_seven_segment_code(uint32_t codepoint);

/* The timecode display (host to surface), left to right, as 7-segment codes: one CC per
 * digit, the leftmost on timecode_cc + digits - 1 down to the rightmost on timecode_cc. The
 * text is cut to the map's digit count and right-aligned with blanks. Writes *n messages
 * into out[0..cap); MACKIE_E_SPACE when cap is short of them. */
int mackie_timecode_encode(const struct mackie_map *m, const char *text, struct mackie_msg *out,
			   size_t cap, size_t *n);

/* The 2-character assignment display (host to surface): two CCs, the left digit
 * (assignment_cc + 1) then the right (assignment_cc). Shorter text is padded with a blank. */
int mackie_assignment_encode(const struct mackie_map *m, const char *text,
			     struct mackie_msg out[2]);

/* ==== Host-connection handshake (SysEx): byte layouts confirmed live ======================= */

/* A parsed MCU handshake SysEx (only the messages the host cares about). */
struct mackie_handshake {
	uint8_t sub;                              /* one of MACKIE_HS_* */
	uint8_t serial[MACKIE_SERIAL_LEN];        /* may be short for a bare device query */
	uint8_t serial_len;
	uint8_t challenge[MACKIE_CHALLENGE_LEN];  /* 4 bytes for HOST_CONNECTION_QUERY/REPLY */
	uint8_t challenge_len;
};

/* An MCU SysEx F0 00 00 66 <model> <sub> body F7: the body's first 7 bytes are the serial,
 * the next 4 the challenge. MACKIE_E_NOT_MINE for another manufacturer, another model or a
 * message that is not SysEx. */
int mackie_handshake_decode(const uint8_t *msg, size_t len, uint8_t model,
			    struct mackie_handshake *out);

/* The Logic/Mackie Control 4-byte challenge-to-response transform (host side). The X-Touch
 * (fw V1.22) accepts ANY reply: it sends neither 0x03 confirm nor 0x04 error, so this
 * formula is unverifiable against that unit; it is kept (and sent) for spec-compliant
 * Logic/Mackie surfaces that do gate on it. A challenge shorter than 4 reads its missing
 * bytes as 0. Each response byte is 7-bit. */
void mackie_handshake_response(const uint8_t *challenge, size_t n,
			       uint8_t response[MACKIE_CHALLENGE_LEN]);

/* A device query (host to surface): asks "who is out there?". The X-Touch answers within
 * about 1 ms with a HOST_CONNECTION_QUERY carrying its serial + challenge. Observed:
 * F0 00 00 66 14 00 F7. */
int mackie_device_query_encode(uint8_t model, struct mackie_msg *out);

/* A host-connection reply (host to surface): echo the surface's serial + the 4-byte response
 * to its challenge (at most 7 and 4 bytes of each are sent). Sent live as
 * F0 00 00 66 14 02 30 31 35 36 34 30 42 33 76 17 2A F7 (serial "015640B", response for
 * challenge "0B8E"); the X-Touch accepted it silently. */
int mackie_host_connection_reply_encode(const uint8_t *serial, size_t serial_len,
					const uint8_t *response, size_t response_len,
					uint8_t model, struct mackie_msg *out);

/* A firmware-version request (host to surface). Observed: F0 00 00 66 14 13 00 F7; the
 * X-Touch replies F0 00 00 66 14 14 56 31 2E 32 32 F7 (ASCII "V1.22"). */
int mackie_version_request_encode(uint8_t model, struct mackie_msg *out);

/* A version reply's text, its bytes as sent, NUL-terminated in text[0..cap). Returns the
 * text's length, MACKIE_E_NOT_MINE when msg is not a version reply, MACKIE_E_SPACE when
 * cap cannot hold it and the NUL. */
int mackie_version_reply_decode(const uint8_t *msg, size_t len, uint8_t model, char *text,
				size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MACKIE_CODEC_H */

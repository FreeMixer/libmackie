/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * libmackie: the Mackie Control Universal (MCU) wire protocol, the language a Behringer
 * X-Touch (and the X-Touch Mini in "MC" mode) speaks over MIDI. The status bytes, the message
 * buffer, the return codes and the control map every encoder and decoder reads.
 *
 * Nothing here opens a port, reads a clock or allocates. The host owns its MIDI transport and
 * carries the bytes; libmackie only says what they are.
 *
 * MIDI status bytes used here:
 *  - 0x8n/0x9n Note Off/On: buttons (SEL/MUTE/SOLO/REC, transport, navigation, fader-touch,
 *    V-Pot push). Host to surface a Note On lights the LED (0x7F on, 0x01 flashing, 0x00
 *    off); surface to host it is a press (0x7F) or a release (0).
 *  - 0xBn Control Change: V-Pot rotation (relative, in) and V-Pot LED ring (out).
 *  - 0xEn Pitch Bend: motor faders (14-bit position), one MIDI channel per fader.
 *  - 0xDn Channel Pressure: channel meter levels (host to surface).
 *  - 0xF0 ... 0xF7 SysEx: LCD scribble strips, device reset, and the host-connection
 *    handshake.
 *
 * The handshake byte sequences were captured live against a Behringer X-Touch (firmware
 * V1.22, MCU mode, ALSA seq, 2026-07-03); see the MACKIE_HS_* constants for the observed
 * exchange. Values still unpinned on real hardware (the meter overload nibble; the
 * challenge-to-response formula itself, which the X-Touch does not enforce) keep the
 * widely documented choice and say so where they are defined.
 */
#ifndef MACKIE_H
#define MACKIE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MACKIE_VERSION_MAJOR  0
#define MACKIE_VERSION_MINOR  1
#define MACKIE_VERSION_PATCH  0
#define MACKIE_VERSION_STRING "0.1.0"
/* The soname's major: libmackie.so.MACKIE_ABI. packaging/libmackie.spec and debian/ carry
 * copies; tests/conformance-packaging.sh holds them to it, and the version to the VERSION
 * file. */
#define MACKIE_ABI            0

/* Return codes: 0 is success, every failure is a negative code. Classify by code,
 * mackie_strerror() is for logs only. */
enum mackie_status {
	MACKIE_OK          = 0,
	MACKIE_E_ARG       = -1, /* a NULL pointer or an impossible argument */
	MACKIE_E_SPACE     = -2, /* the output buffer is too small */
	MACKIE_E_RANGE     = -3, /* a value outside what the wire or the map can carry */
	MACKIE_E_NOT_MINE  = -4, /* not a message of this kind (a decoder's "null") */
};

const char *mackie_strerror(int code);

/* ---- MIDI status nibbles ------------------------------------------------------------- */
/* Note On status (channel 0). Note Off is normally sent as Note On velocity 0. */
#define MACKIE_NOTE_ON          0x90
/* Note Off status (channel 0). */
#define MACKIE_NOTE_OFF         0x80
/* Control Change status (channel 0). */
#define MACKIE_CONTROL_CHANGE   0xb0
/* Pitch Bend status (channel n = fader index). */
#define MACKIE_PITCH_BEND       0xe0
/* Channel Pressure (mono aftertouch) status: carries meter levels. */
#define MACKIE_CHANNEL_PRESSURE 0xd0
/* SysEx start / end. */
#define MACKIE_SYSEX_START      0xf0
#define MACKIE_SYSEX_END        0xf7

/* LED / press velocities. */
#define MACKIE_LED_ON    0x7f
#define MACKIE_LED_FLASH 0x01
#define MACKIE_LED_OFF   0x00

/* Number of channel strips on a full X-Touch (one MCU bank). */
#define MACKIE_STRIPS 8
/* Pitch-bend full scale (14-bit). */
#define MACKIE_PITCHBEND_MAX 0x3fff /* 16383 */

/* ---- SysEx framing ------------------------------------------------------------------- */
/* Mackie/Behringer SysEx manufacturer id (00 00 66). */
#define MACKIE_MANUFACTURER_0 0x00
#define MACKIE_MANUFACTURER_1 0x00
#define MACKIE_MANUFACTURER_2 0x66
/* Model id byte in the SysEx header. 0x14 = Mackie Control (what the X-Touch answers to in
 * MCU mode); 0x10 = Logic Control, 0x11 = LC XT. Every SysEx encoder takes it, per
 * surface. */
#define MACKIE_MODEL_MCU 0x14
/* SysEx sub-command: LCD scribble-strip write. */
#define MACKIE_SYSEX_LCD 0x12
/* SysEx sub-command: device reset (clears LCD + all LEDs). */
#define MACKIE_SYSEX_RESET 0x08
/* SysEx sub-command: the eight scribble backlights, one colour byte per strip. */
#define MACKIE_SYSEX_SCRIBBLE_COLOUR 0x72

/* Handshake / device-management SysEx sub-commands, with the roles as OBSERVED LIVE on a
 * Behringer X-Touch (firmware V1.22, MCU mode, 2026-07-03). The HOST initiates:
 *
 *   host  -> F0 00 00 66 14 00 F7                                   (DEVICE_QUERY)
 *   device-> F0 00 00 66 14 01 30 31 35 36 34 30 42 30 42 38 45 F7  (HOST_CONNECTION_QUERY:
 *                             '-- serial "015640B" --' '-"0B8E"-'     serial 7 + challenge 4)
 *   host  -> F0 00 00 66 14 02 <serial 7> <response 4> F7           (HOST_CONNECTION_REPLY)
 *   device-> (nothing: no 0x03 confirm and no 0x04 error, even for a WRONG response)
 *
 * So on the X-Touch the challenge/response is vestigial: the device identifies itself when
 * asked but gates nothing on the reply, and it never volunteers the query at idle (a spec
 * Logic/Mackie Control sends 0x01 unsolicited at power-on: handle both). Queries addressed
 * to other model ids (0x10 Logic Control) go unanswered. */
#define MACKIE_HS_DEVICE_QUERY          0x00 /* host to surface: "who is out there?" */
#define MACKIE_HS_HOST_CONNECTION_QUERY 0x01 /* surface to host: serial (7) + challenge (4) */
#define MACKIE_HS_HOST_CONNECTION_REPLY 0x02 /* host to surface: serial (7) + response (4) */
#define MACKIE_HS_CONNECTION_CONFIRM    0x03 /* surface to host (spec): accepted; the X-Touch never sends it */
#define MACKIE_HS_CONNECTION_ERROR      0x04 /* surface to host (spec): rejected; the X-Touch never sends it */
#define MACKIE_HS_VERSION_REQUEST       0x13 /* host to surface: firmware version request (body 0x00) */
#define MACKIE_HS_VERSION_REPLY         0x14 /* surface to host: ASCII version text (X-Touch: "V1.22") */

#define MACKIE_SERIAL_LEN    7
#define MACKIE_CHALLENGE_LEN 4

/* The X-Touch LCD is two rows of 56 characters (112 cells total). */
#define MACKIE_LCD_ROW_CHARS 56
#define MACKIE_LCD_ROWS      2
/* Each strip owns a 7-character slice of its row. */
#define MACKIE_STRIP_CELL_CHARS 7

/* Meter LED count that reads as "full scale" on an MCU strip meter. */
#define MACKIE_METER_SEGMENTS 12

/* ---- one message --------------------------------------------------------------------- */

/* The longest message an encoder builds is a scribble write over the whole LCD:
 * 7 header bytes + 112 cells + F7 = 120. */
#define MACKIE_MSG_MAX 128

struct mackie_msg {
	uint8_t bytes[MACKIE_MSG_MAX];
	uint8_t len;
};

/* ---- the control map ------------------------------------------------------------------- */
/* Every wire address the codec decodes or paints. The host builds it from its surface
 * profile, the one source of every address; libmackie keeps only the protocol (how a note,
 * CC, pitch bend or SysEx is encoded) and never a second table of numbers beside the
 * profile. */

/* The per-strip buttons, in the order the map holds them. */
enum mackie_strip_button {
	MACKIE_STRIP_REC = 0,
	MACKIE_STRIP_SOLO,
	MACKIE_STRIP_MUTE,
	MACKIE_STRIP_SELECT,
	MACKIE_STRIP_VPOT_PUSH,
	MACKIE_STRIP_BUTTONS
};

/* The global keys the codec names, in the order the map holds them. */
enum mackie_global {
	MACKIE_G_BANK_LEFT = 0,
	MACKIE_G_BANK_RIGHT,
	MACKIE_G_CHANNEL_LEFT,
	MACKIE_G_CHANNEL_RIGHT,
	MACKIE_G_FLIP,
	MACKIE_G_MARKER,
	MACKIE_G_CYCLE,
	MACKIE_G_REWIND,
	MACKIE_G_FFWD,
	MACKIE_G_STOP,
	MACKIE_G_PLAY,
	MACKIE_G_RECORD,
	/* the modifier row under the display */
	MACKIE_G_SHIFT,
	MACKIE_G_OPTION,
	MACKIE_G_CONTROL,
	MACKIE_G_ALT,
	MACKIE_GLOBALS
};

/* Any other key of the profile (F-keys, the automation and utility blocks, lamps): the
 * decoder reports it as MACKIE_BTN_KEY with this id, which the host owns. */
struct mackie_key {
	uint8_t note;
	const char *id;
};

/* The longest timecode display the map may declare. */
#define MACKIE_DISPLAY_MAX_DIGITS 16

struct mackie_map {
	/* note of each per-strip button, [button][strip 0..7] */
	uint8_t strip_note[MACKIE_STRIP_BUTTONS][MACKIE_STRIPS];
	/* fader-touch notes, strips 0..7 then the master at index MACKIE_STRIPS */
	uint8_t touch_note[MACKIE_STRIPS + 1];
	/* V-Pot turn CC of encoder 0..7 */
	uint8_t vpot_cc[MACKIE_STRIPS];
	/* LED-ring CC of encoder 0..7 */
	uint8_t ring_cc[MACKIE_STRIPS];
	/* bit n set: pitch-bend channel n is a fader (0..7 strips, 8 master) */
	uint16_t fader_channels;
	/* note of each global key */
	uint8_t global_note[MACKIE_GLOBALS];
	/* the 2-character assignment display: the RIGHT digit's CC; the left one is +1 */
	uint8_t assignment_cc;
	/* the timecode display: the RIGHTMOST digit's CC, and its digit count; the digits run
	 * up from the rightmost */
	uint8_t timecode_cc;
	uint8_t timecode_digits;
	/* the jog wheel's CC, or -1 for a surface without one */
	int16_t jog_cc;
	/* the profile's other keys; NULL when n_keys is 0 */
	const struct mackie_key *keys;
	size_t n_keys;
};

/* MACKIE_OK for a map every encoder and decoder can use: every note and CC 7-bit, the
 * display digits 1..MACKIE_DISPLAY_MAX_DIGITS with every digit's CC 7-bit, fader channels
 * within 0..15, every key with an id, and no note named twice. MACKIE_E_RANGE otherwise,
 * never a partial map; MACKIE_E_ARG for NULL. */
int mackie_map_check(const struct mackie_map *m);

#ifdef __cplusplus
}
#endif

#endif /* MACKIE_H */

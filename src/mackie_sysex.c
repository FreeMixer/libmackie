/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * The SysEx messages: scribble strips and their backlights, the device reset, and the
 * host-connection handshake (byte layouts confirmed live, see MACKIE_HS_* in mackie.h).
 */
#include <string.h>
#include <mackie/mackie_codec.h>
#include "mackie_text.h"

#define LCD_CELLS (MACKIE_LCD_ROW_CHARS * MACKIE_LCD_ROWS)

/* F0 00 00 66 model sub: the six bytes every message here starts with. */
static void head(struct mackie_msg *out, uint8_t model, uint8_t sub)
{
	out->bytes[0] = MACKIE_SYSEX_START;
	out->bytes[1] = MACKIE_MANUFACTURER_0;
	out->bytes[2] = MACKIE_MANUFACTURER_1;
	out->bytes[3] = MACKIE_MANUFACTURER_2;
	out->bytes[4] = model;
	out->bytes[5] = sub;
	out->len = 6;
}

static void put(struct mackie_msg *out, uint8_t b)
{
	out->bytes[out->len++] = b;
}

/* A cell shows printable ASCII; anything else is a space. */
static uint8_t cell(uint32_t cp)
{
	return cp >= 0x20 && cp <= 0x7e ? (uint8_t)cp : 0x20;
}

/* The write of chars[0..n) at offset, clamped to the cells left after it. */
static void scribble(unsigned offset, const uint32_t *chars, size_t n, uint8_t model,
		     struct mackie_msg *out)
{
	size_t max = offset < LCD_CELLS ? LCD_CELLS - offset : 0;

	head(out, model, MACKIE_SYSEX_LCD);
	put(out, offset & 0x7f);
	for (size_t i = 0; i < n && i < max; i++)
		put(out, cell(chars[i]));
	put(out, MACKIE_SYSEX_END);
}

int mackie_scribble_encode(unsigned offset, const char *text, uint8_t model,
			   struct mackie_msg *out)
{
	uint32_t chars[LCD_CELLS];
	size_t n = 0;
	uint32_t c;

	if (!text || !out)
		return MACKIE_E_ARG;
	while (n < LCD_CELLS && (c = mackie_utf8_next(&text)) != 0)
		chars[n++] = c;
	scribble(offset, chars, n, model, out);
	return MACKIE_OK;
}

int mackie_strip_scribble_encode(unsigned strip, unsigned row, const char *text, uint8_t model,
				 struct mackie_msg *out)
{
	uint16_t u[MACKIE_STRIP_CELL_CHARS];
	uint32_t chars[MACKIE_STRIP_CELL_CHARS];

	if (!text || !out)
		return MACKIE_E_ARG;
	if (row > 1)
		return MACKIE_E_RANGE;
	/* cut to 7, pad with spaces to 7 */
	size_t got = mackie_utf16_head(text, u, MACKIE_STRIP_CELL_CHARS);
	for (size_t i = got; i < MACKIE_STRIP_CELL_CHARS; i++)
		u[i] = ' ';
	size_t n = mackie_utf16_chars(u, MACKIE_STRIP_CELL_CHARS, chars);
	scribble(row * MACKIE_LCD_ROW_CHARS + strip * MACKIE_STRIP_CELL_CHARS, chars, n, model, out);
	return MACKIE_OK;
}

/* ---- Scribble backlight colour ---- */

const struct mackie_colour mackie_scribble_colours[MACKIE_SCRIBBLE_COLOURS] = {
	{ "off",     { 0, 0, 0 } },
	{ "red",     { 255, 0, 0 } },
	{ "green",   { 0, 255, 0 } },
	{ "yellow",  { 255, 255, 0 } },
	{ "blue",    { 0, 0, 255 } },
	{ "magenta", { 255, 0, 255 } },
	{ "cyan",    { 0, 255, 255 } },
	{ "white",   { 255, 255, 255 } },
};

int mackie_scribble_colour_index(const char *name)
{
	if (!name)
		return MACKIE_E_ARG;
	for (int i = 0; i < MACKIE_SCRIBBLE_COLOURS; i++)
		if (strcmp(mackie_scribble_colours[i].name, name) == 0)
			return i;
	return MACKIE_E_RANGE;
}

int mackie_scribble_colours_encode(const unsigned *colours, size_t n, uint8_t model,
				   struct mackie_msg *out)
{
	if (!out || (n && !colours))
		return MACKIE_E_ARG;
	if (n != MACKIE_STRIPS)
		return MACKIE_E_RANGE;
	for (size_t i = 0; i < n; i++)
		if (colours[i] >= MACKIE_SCRIBBLE_COLOURS)
			return MACKIE_E_RANGE;
	head(out, model, MACKIE_SYSEX_SCRIBBLE_COLOUR);
	for (size_t i = 0; i < n; i++)
		put(out, (uint8_t)colours[i]);
	put(out, MACKIE_SYSEX_END);
	return MACKIE_OK;
}

int mackie_reset_encode(uint8_t model, struct mackie_msg *out)
{
	if (!out)
		return MACKIE_E_ARG;
	head(out, model, MACKIE_SYSEX_RESET);
	put(out, 0x00);
	put(out, MACKIE_SYSEX_END);
	return MACKIE_OK;
}

/* ---- Host-connection handshake ---- */

int mackie_handshake_decode(const uint8_t *msg, size_t len, uint8_t model,
			    struct mackie_handshake *out)
{
	if (!out || (len && !msg))
		return MACKIE_E_ARG;
	/* F0 00 00 66 model sub ... F7: the sub may be the F7 itself, with no body */
	if (len < 6 || msg[0] != MACKIE_SYSEX_START || msg[len - 1] != MACKIE_SYSEX_END)
		return MACKIE_E_NOT_MINE;
	if (msg[1] != MACKIE_MANUFACTURER_0 || msg[2] != MACKIE_MANUFACTURER_1 ||
	    msg[3] != MACKIE_MANUFACTURER_2 || msg[4] != model)
		return MACKIE_E_NOT_MINE;
	const uint8_t *body = msg + 6;
	size_t blen = len > 7 ? len - 7 : 0;
	struct mackie_handshake h = { .sub = msg[5] };
	h.serial_len = (uint8_t)(blen < MACKIE_SERIAL_LEN ? blen : MACKIE_SERIAL_LEN);
	memcpy(h.serial, body, h.serial_len);
	if (blen > MACKIE_SERIAL_LEN) {
		size_t c = blen - MACKIE_SERIAL_LEN;
		h.challenge_len = (uint8_t)(c < MACKIE_CHALLENGE_LEN ? c : MACKIE_CHALLENGE_LEN);
		memcpy(h.challenge, body + MACKIE_SERIAL_LEN, h.challenge_len);
	}
	*out = h;
	return MACKIE_OK;
}

void mackie_handshake_response(const uint8_t *challenge, size_t n,
			       uint8_t response[MACKIE_CHALLENGE_LEN])
{
	int c[MACKIE_CHALLENGE_LEN] = { 0 };

	if (!response)
		return;
	for (size_t i = 0; i < MACKIE_CHALLENGE_LEN && i < n && challenge; i++)
		c[i] = challenge[i];
	/* in int, then reduced through unsigned: the low 7 bits of a two's-complement
	 * difference, as JavaScript's 32-bit bitwise operators give them */
	response[0] = (uint8_t)(0x7fu & (unsigned)(c[0] + (c[1] ^ 0x0a) - c[3]));
	response[1] = (uint8_t)(0x7fu & (unsigned)((c[2] >> 4) ^ (c[0] + c[3])));
	response[2] = (uint8_t)(0x7fu & (unsigned)((c[3] - (c[2] << 2)) ^ (c[0] | c[1])));
	response[3] = (uint8_t)(0x7fu & (unsigned)(c[1] - c[2] + (0xf0 ^ (c[3] << 4))));
}

int mackie_device_query_encode(uint8_t model, struct mackie_msg *out)
{
	if (!out)
		return MACKIE_E_ARG;
	head(out, model, MACKIE_HS_DEVICE_QUERY);
	put(out, MACKIE_SYSEX_END);
	return MACKIE_OK;
}

int mackie_host_connection_reply_encode(const uint8_t *serial, size_t serial_len,
					const uint8_t *response, size_t response_len,
					uint8_t model, struct mackie_msg *out)
{
	if (!out || (serial_len && !serial) || (response_len && !response))
		return MACKIE_E_ARG;
	head(out, model, MACKIE_HS_HOST_CONNECTION_REPLY);
	for (size_t i = 0; i < serial_len && i < MACKIE_SERIAL_LEN; i++)
		put(out, serial[i]);
	for (size_t i = 0; i < response_len && i < MACKIE_CHALLENGE_LEN; i++)
		put(out, response[i]);
	put(out, MACKIE_SYSEX_END);
	return MACKIE_OK;
}

int mackie_version_request_encode(uint8_t model, struct mackie_msg *out)
{
	if (!out)
		return MACKIE_E_ARG;
	head(out, model, MACKIE_HS_VERSION_REQUEST);
	put(out, 0x00);
	put(out, MACKIE_SYSEX_END);
	return MACKIE_OK;
}

int mackie_version_reply_decode(const uint8_t *msg, size_t len, uint8_t model, char *text,
				size_t cap)
{
	struct mackie_handshake h;

	if (!text || (len && !msg))
		return MACKIE_E_ARG;
	int rc = mackie_handshake_decode(msg, len, model, &h);
	if (rc)
		return rc;
	if (h.sub != MACKIE_HS_VERSION_REPLY)
		return MACKIE_E_NOT_MINE;
	size_t n = len > 7 ? len - 7 : 0;
	if (n + 1 > cap || n > 0x7fffffff)
		return MACKIE_E_SPACE;
	memcpy(text, msg + 6, n);
	text[n] = '\0';
	return (int)n;
}

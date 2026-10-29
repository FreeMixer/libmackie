/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * Return codes and the control map's check.
 */
#include <mackie/mackie.h>

const char *mackie_strerror(int code)
{
	switch (code) {
	case MACKIE_OK:         return "ok";
	case MACKIE_E_ARG:      return "invalid argument";
	case MACKIE_E_SPACE:    return "output buffer too small";
	case MACKIE_E_RANGE:    return "value out of range";
	case MACKIE_E_NOT_MINE: return "message of another kind";
	default:                return "unknown error";
	}
}

/* Each note may name one control: seen[] counts them. */
static int note_once(uint8_t seen[128], unsigned note)
{
	if (note > 0x7f || seen[note])
		return 0;
	seen[note] = 1;
	return 1;
}

int mackie_map_check(const struct mackie_map *m)
{
	uint8_t seen[128] = { 0 };

	if (!m || (m->n_keys && !m->keys))
		return MACKIE_E_ARG;
	for (unsigned b = 0; b < MACKIE_STRIP_BUTTONS; b++)
		for (unsigned s = 0; s < MACKIE_STRIPS; s++)
			if (!note_once(seen, m->strip_note[b][s]))
				return MACKIE_E_RANGE;
	for (unsigned s = 0; s <= MACKIE_STRIPS; s++)
		if (!note_once(seen, m->touch_note[s]))
			return MACKIE_E_RANGE;
	for (unsigned g = 0; g < MACKIE_GLOBALS; g++)
		if (!note_once(seen, m->global_note[g]))
			return MACKIE_E_RANGE;
	for (size_t k = 0; k < m->n_keys; k++)
		if (!m->keys[k].id || !note_once(seen, m->keys[k].note))
			return MACKIE_E_RANGE;
	for (unsigned s = 0; s < MACKIE_STRIPS; s++)
		if (m->vpot_cc[s] > 0x7f || m->ring_cc[s] > 0x7f)
			return MACKIE_E_RANGE;
	/* the assignment display's left digit is one CC above its right */
	if (m->assignment_cc >= 0x7f)
		return MACKIE_E_RANGE;
	if (m->timecode_digits < 1 || m->timecode_digits > MACKIE_DISPLAY_MAX_DIGITS ||
	    m->timecode_cc + m->timecode_digits - 1 > 0x7f)
		return MACKIE_E_RANGE;
	if (m->jog_cc < -1 || m->jog_cc > 0x7f)
		return MACKIE_E_RANGE;
	return MACKIE_OK;
}

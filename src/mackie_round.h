/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
 *
 * Internal: rounding as the TypeScript did it (Math.round), for the non-negative values the
 * encoders round, without libm.
 */
#ifndef MACKIE_ROUND_H
#define MACKIE_ROUND_H

/* The integer nearest x, a half rounding up; x is in [0, 2^31) or NaN, and NaN gives 0 (a
 * NaN reaches the wire as 0 through JavaScript's bitwise operators). x - trunc(x) is exact
 * for these x, so 0.49999999999999994 rounds down, as Math.round rounds it. */
static inline unsigned mackie_round_nonneg(double x)
{
	if (!(x >= 0))
		return 0;
	unsigned r = (unsigned)x;
	return x - r >= 0.5 ? r + 1 : r;
}

#endif

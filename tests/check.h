// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>

/* The checks the C suites share: a failed check prints where and what, and is counted; a suite
 * exits non-zero when any failed. */
#ifndef MACKIE_TEST_CHECK_H
#define MACKIE_TEST_CHECK_H

#include <stdio.h>
#include <string.h>
#include <mackie/mackie.h>

static int fails, checks;

#define CHK(cond) do { \
	checks++; \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

/* The bytes of msg are want[0..n); on a difference both are printed under what. */
static int same_bytes(const char *what, const uint8_t *got, size_t got_len, const uint8_t *want,
		      size_t n)
{
	checks++;
	if (got_len == n && memcmp(got, want, n) == 0)
		return 1;
	fails++;
	fprintf(stderr, "FAIL %s\n  got: ", what);
	for (size_t i = 0; i < got_len; i++)
		fprintf(stderr, " %02x", got[i]);
	fprintf(stderr, "\n want:");
	for (size_t i = 0; i < n; i++)
		fprintf(stderr, " %02x", want[i]);
	fprintf(stderr, "\n");
	return 0;
}

#define MSG_IS(what, msg, ...) do { \
	static const uint8_t want_[] = { __VA_ARGS__ }; \
	same_bytes((what), (msg).bytes, (msg).len, want_, sizeof want_); \
} while (0)

static int verdict(const char *name)
{
	if (fails) {
		fprintf(stderr, "%s: %d of %d checks failed\n", name, fails, checks);
		return 1;
	}
	printf("%s: ok (%d checks)\n", name, checks);
	return 0;
}

#endif

#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Pau Aliagas <linuxnow@gmail.com>
#
# THE COVERAGE WITNESS (`make fuzz-coverage`). Each harness, built for llvm-cov without
# sanitizers, runs once over its seed corpus (-runs=0 executes the corpus and nothing else);
# this prints the line coverage of the reader functions the harness exists to drive, and
# every line of them the corpus never reached. A file's other functions (log strings, the
# socket code) are not the harness's job and are not counted.
#
# THE FLOOR. fuzz/coverage-floor.txt holds, per harness, the number of covered lines the
# committed corpus reaches. A harness whose covered lines fall below its floor turns this
# red: a seed lost, a harness that stopped driving a function, or a reader that grew lines
# its corpus never reaches. Raise a floor when coverage rises; lower one only together with
# the change that removed the lines.
#
# THE HARNESS FLOOR. Every property check of the harness itself, each abort() in
# fuzz/fuzz_<harness>.c and the statement that guards it (fuzz/checks.awk), must be executed
# by at least one seed. A check no seed reaches is a property nobody tests, and the
# library's line count cannot see it; this names the harness file and the line of every
# such check and turns red. So does a check whose comment holds no `mutant:` and `input:`
# record and no `not a property:` line (fuzz/records.sh proves the records): a check that
# names no bug it catches is a property nobody showed can fail.
#
# Usage: coverage.sh <cov build dir> <harness>...   (LLVM_COV names llvm-cov)
set -eu
B=$1
shift
LLVM_COV=${LLVM_COV:-llvm-cov}
FLOOR=fuzz/coverage-floor.txt
[ -f "$FLOOR" ] || { echo "coverage.sh: no $FLOOR" >&2; exit 2; }
low=
dead=

# The functions each harness covers: what "every reader line the harness means to cover"
# means, one list per harness. Static functions are matched by bare name.
funcs() {
	case $1 in
	decode) echo src/mackie_codec.c :: \
		lookup mackie_fader_decode mackie_button_decode relative mackie_vpot_decode \
		mackie_jog_decode ;;
	sysex) echo src/mackie_sysex.c :: \
		mackie_handshake_decode mackie_handshake_response mackie_version_reply_decode ;;
	text) echo src/mackie_text.c src/mackie_display.c src/mackie_sysex.c :: \
		mackie_utf8_next mackie_utf16_head mackie_utf16_chars mackie_seven_segment_code \
		mackie_timecode_encode mackie_assignment_encode head put cell scribble \
		mackie_scribble_encode mackie_strip_scribble_encode ;;
	map) echo src/mackie_map.c :: mackie_strerror note_once mackie_map_check ;;
	encode) echo src/mackie_codec.c src/mackie_sysex.c :: \
		put3 mackie_fader_encode14 mackie_fader_encode mackie_button_led_encode \
		mackie_strip_note mackie_touch_note mackie_ring_encode mackie_meter_encode \
		mackie_scribble_colour_index mackie_scribble_colours_encode mackie_reset_encode \
		mackie_device_query_encode mackie_host_connection_reply_encode \
		mackie_version_request_encode ;;
	*) echo "coverage.sh: no function list for harness '$1'" >&2; exit 2 ;;
	esac
}

for h in "$@"; do
	set -- $(funcs "$h")
	files=
	while [ "$1" != "::" ]; do files="$files $1"; shift; done
	shift
	bin=$B/fuzz_$h prof=$B/$h.profdata
	echo "== fuzz_$h: $(ls fuzz/corpus/$h | wc -l) seed inputs"
	# Name Regions Miss Cover Lines Miss Cover ...: keep the listed functions.
	tot=$B/$h.covered
	"$LLVM_COV" report -show-functions "$bin" -instr-profile="$prof" $files |
	awk -v want="$*" -v tot="$tot" '
		BEGIN { n = split(want, w, " "); for (i = 1; i <= n; i++) keep[w[i]] = 1 }
		{ name = $1; sub(/^.*:/, "", name) }
		(name in keep) && NF >= 10 {
			printf "  %-34s lines %4d  missed %3d  %s\n", name, $5, $6, $7
			lines += $5; missed += $6; seen[name] = 1
		}
		END {
			for (k in keep) if (!(k in seen)) printf "  %-34s NOT FOUND in the report\n", k
			printf "  %-34s lines %4d  missed %3d  %.2f%%\n", "TOTAL", lines, missed,
			       lines ? 100 * (lines - missed) / lines : 0
			print lines - missed > tot
		}'
	for f in "$@"; do
		"$LLVM_COV" show "$bin" -instr-profile="$prof" -name-regex="^(.*:)?$f\$" $files \
			2>/dev/null |
		awk -v f="$f" -F'|' '$2 ~ /^ *0$/ {
			sub(/^ */, "", $1); printf "    unreached %s:%s %s\n", f, $1, $3 }'
	done
	src=fuzz/fuzz_$h.c
	"$LLVM_COV" show "$bin" -instr-profile="$prof" "$src" |
		awk -v file="$src" -f fuzz/checks.awk >"$B/$h.checks"
	nchk=$(wc -l <"$B/$h.checks")
	if [ "$nchk" -eq 0 ]; then
		echo "  CHECKS: no property check found in $src"
		dead="$dead $h"
	elif awk -F: '$4 == "0" && $5 != "not-a-property" { n++ } END { exit !n }' "$B/$h.checks"; then
		awk -F: '$4 == "0" && $5 != "not-a-property" { t = $0; sub(/^([^:]*:){5}/, "", t)
			printf "  CHECK: %s:%d no seed reaches (abort at line %d): %s\n", $1, $2, $3, t }' \
			"$B/$h.checks"
		dead="$dead $h"
	elif awk -F: '$5 == "NONE" || $5 == "BAD" { n++ } END { exit !n }' "$B/$h.checks"; then
		awk -F: '$5 == "NONE" || $5 == "BAD" { t = $0; sub(/^([^:]*:){5}/, "", t)
			printf "  RECORD: %s:%d has no mutant: and input: record and is no stop after a failed allocation (%s): %s\n",
			       $1, $3, $5, t }' "$B/$h.checks"
		dead="$dead $h"
	else
		echo "  checks: all $nchk property checks of $src reached"
	fi
	covered=$(cat "$tot")
	floor=$(awk -v h="$h" '$1 == h { print $2 }' "$FLOOR")
	if [ -z "$floor" ]; then
		echo "  FLOOR: fuzz_$h has no line in $FLOOR"
		low="$low $h"
	elif [ "$covered" -lt "$floor" ]; then
		echo "  FLOOR: fuzz_$h covers $covered reader lines, below its floor of $floor"
		low="$low $h"
	else
		echo "  floor $floor: held ($covered covered)"
	fi
done

[ -z "$low" ] || echo "make fuzz-coverage: RED -- below the coverage floor:$low"
[ -z "$dead" ] || echo "make fuzz-coverage: RED -- harness checks no seed reaches, or with no record:$dead"
[ -z "$low$dead" ] || exit 1
echo "make fuzz-coverage: every harness at or above its coverage floor, every check reached"

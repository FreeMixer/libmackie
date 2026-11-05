# Fuzzing libmackie

libmackie reads what a Mackie Control surface sends and the text a host shows on it: the channel
messages (Note On and Off, Pitch Bend, Control Change), the SysEx of the host-connection
handshake, UTF-8 text for the scribble strips and the seven-segment displays, and the control map
the host builds. It also encodes what the host sends back. Each of these surfaces has a libFuzzer
harness here, built with clang under AddressSanitizer and UndefinedBehaviorSanitizer and run in CI on
every push and pull request (`.github/workflows/fuzz.yml`). The harnesses are written to the build
contract of OSS Verify (its `docs/build-contract.md`, "The checks of a fuzz target").

| harness | reads | functions |
|---|---|---|
| `fuzz_decode` | MIDI channel messages from the surface | `mackie_button_decode`, `mackie_fader_decode`, `mackie_vpot_decode`, `mackie_jog_decode` |
| `fuzz_sysex` | handshake and version SysEx | `mackie_handshake_decode`, `mackie_version_reply_decode`, `mackie_handshake_response` |
| `fuzz_text` | UTF-8 text | the scribble writers, `mackie_timecode_encode`, `mackie_assignment_encode`, `mackie_seven_segment_code` |
| `fuzz_map` | a control map | `mackie_map_check`, `mackie_strerror` |
| `fuzz_encode` | numbers and bytes the host chooses | the fader, LED, ring, meter, colour and fixed SysEx encoders, the note lookups |

Each harness is one plain C file defining `LLVMFuzzerTestOneInput`, whose header comment is the
grammar of its input: a few bytes that choose the map or the model id, then operations until the
input ends. Every input means something and the end of the input ends it. No harness reads a
clock, the environment, a file or the network, starts a thread or installs a signal handler. An
input of 64 MiB or more is refused (-1); every other input returns 0. A message or text is copied
into an allocation of exactly its length, so a read one byte past it is a sanitizer report.

## The expected values come from the protocol, not from the library

No check compares libmackie with itself. `fuzz/spec.h` holds the wire format as the harnesses read
it: the status bytes and sub-commands, RFC 3629's table of well-formed UTF-8, the UTF-16 slice and
iteration the header describes, the seven-segment folds, and a rounding done in integers. The
handshake response is held to the exchange captured live from an X-Touch, as `mackie.h` records it.
Each check carries a `contract:` line that names what it stands on.

## The checks

Each property is an `if` that calls `abort()` in a `noinline` `check_<property>` function. In the
comment above it, a `mutant:` / `input:` record names a one-line change to `src/` or `include/` and
the seed that shows it. `make fuzz-records` builds the harness plain and native, makes each change
alone and requires the seed to pass without it and to abort at that check with it. A check with
neither a record nor a `not a property:` line is red before anything is built. `fuzz/sabotage/`
holds one patch per property; `make fuzz-sabotage` requires each to be found by the seed its header
names, in the property its file name names, and by no sanitizer report.

## Targets

    make fuzz                    build every harness (clang, libFuzzer, ASan, UBSan)
    make fuzz-run FUZZ_TIME=60   fuzz each from its seed corpus; a crash is left in fuzz/findings/
    make fuzz-records            every record met
    make fuzz-sabotage           every sabotage patch found in its own property
    make fuzz-coverage           reader line coverage over the seeds held to fuzz/coverage-floor.txt,
                                 and every check reached
    make fuzz-static             each harness linked fully static over its corpus
    make fuzz-corpus             write fuzz/corpus/ again
    make fuzz-corpus-check       red when fuzz/corpus/ is not what fuzz-corpus writes

The seed corpora are in `fuzz/corpus/<harness>/`, written by `fuzz/gen-corpus.c` from the control
map and byte strings of the protocol, never from a capture or the network. Raise a coverage floor
when coverage rises; lower one only with the change that removed the lines.

## Not covered

The fader decoder reads its two data bytes as they come. A byte with the high bit set is not MIDI
data, and the decoded value then exceeds 16383. The harness holds the value only where both bytes
are 7-bit, and the fuzzer's other properties (refusals, channel, no overread) hold everywhere.

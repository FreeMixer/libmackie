# libmackie

A C library for the Mackie Control (MCU) protocol that motorised MIDI control surfaces such as
the Behringer X-Touch speak: encoders and decoders for motor faders, buttons and their lamps,
V-Pots and their LED rings, the jog wheel, meters, scribble strips and their backlights, the
seven-segment displays and the host-connection handshake.

It is a codec and nothing else. Every function is pure, over caller memory: no MIDI port, no
clock, no thread, no allocation. The host already owns a MIDI transport (ALSA sequencer, raw
MIDI, a DAW's own) and carries the bytes; libmackie says what they are. Which note, CC or
pitch-bend channel each control sits on comes from the host's own surface profile, as a
`struct mackie_map`.

## Install

Fedora 44:

```
sudo dnf install https://www.openmixer.org/rpm/openmixer-release.noarch.rpm
sudo dnf install libmackie-devel
```

Debian bookworm and trixie (also Raspberry Pi OS):

```
curl -fsSLO https://www.openmixer.org/deb/openmixer-release.deb && sudo apt install ./openmixer-release.deb
sudo apt install libmackie-dev
```

## Start

```c
#include <mackie/mackie.h>
```

```
cc app.c $(pkg-config --cflags --libs libmackie)
```

## Documentation

<https://www.openmixer.org/projects/libmackie>, and in this repository: where the library sits,
the control map and how the codec is held to its TypeScript original
([docs/overview.md](docs/overview.md)); building and testing ([BUILDING.md](BUILDING.md)).

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

# OPHIS-DRONE

A drone synthesizer for Linux desktop and handhelds. Three oscillator
chains, a 16-slot modulation matrix, per-chain sequencers and master
effects — everything tuned in raw frequencies, no notes or scales.

## What it sounds like

Three chains (A, B, C) drone continuously. Each chain has:

- a main oscillator — sine, triangle, saw, square (with pulse width), white
  or pink noise; the saw and square are band-limited, so no aliasing grit
- two sub-oscillators dividing the main frequency down (÷1 to ÷16)
- an ADSR envelope and a resonant filter (LP/HP/BP/notch, 12 or 24 dB/oct)
- its own 8-step sequencer: forward, ping-pong, odd/even, random or a
  markov walk, with a clock divider and one shared BPM

The chains feed a soft-clipping master bus with stereo delay, reverb and a
safety filter. Any chain can modulate any other — pitch vibrato, phase, FM,
ring mod, filter sweeps, envelope modulation — through a 16-slot matrix
you patch like a modular.

## Using it

The UI is a 640×480 screen with five tabs: `OSC 1 | OSC 2 | OSC 3 | MOD |
MASTER`. The OSC tabs have sub-tabs for the oscillator, its envelope/filter
and its sequencer. MOD is the patch matrix: pick a slot with the SLOT row,
then set its source, target, destination, depth and level. MASTER holds
volume, BPM and the delay/reverb controls. A hint bar at the bottom always
shows the keys in use.

| Action | Desktop | Handheld (R36S / RG28XX) |
|---|---|---|
| Switch tabs | Q / E | L / R shoulder |
| Move selection | Up / Down | d-pad up / down |
| Change value | Z / X | Y / X |
| Navigate (sub-tabs, seq steps, digits) | Left / Right | d-pad left / right |
| TRIGGER (retrigger everything) | T | B |
| RUN (start/stop the transport) | Space | A |
| MENU | Esc | Start |
| Confirm | Enter | A |

Esc opens the menu: NEW, SAVE, LOAD, PANIC, EXIT. Presets are plain JSON
files saved next to the program; LOAD can browse folders. On desktop you
type preset names on the keyboard; on handhelds you pick letters from a
grid.

## Getting it

Prebuilt downloads live on the
[releases page](https://github.com/Ophis-Works/ophis-drone/releases):

- `ophis-drone-desktop.tar.gz` — Linux x86-64, needs SDL2 and ALSA
- `ophis-drone-r36s.zip` — R36S (ArkOS): unzip into `roms/`, launch from
  the Ports menu
- `ophis-drone-rg28xx.zip` — RG28XX: extract at the root of the ROMS
  partition, launch from Ports

Every archive includes the factory presets and the manual (`MANUAL.md`).

## Building from source

Desktop:

```
make            # build/desktop/ophis-drone
make test       # run the unit tests
make desktop    # also package build/deploy/ophis-drone-desktop.tar.gz
```

You need gcc, make, and the SDL2 + ALSA dev packages (pkg-config names:
`sdl2`, `alsa`).

Handhelds are cross-compiled from the same source:

```
make handheld   # R36S binary + zip (needs aarch64-linux-gnu-gcc, arm64 SDL2/ALSA)
make rg28xx     # RG28XX binary + zip
```

Both handheld builds run a gate (`tools/handheld_gate.sh`) that fails the
build unless the binary runs against glibc 2.28 or older, so it works on
ArkOS and EmuELEC firmware as-is.

## License

OPHIS-DRONE is licensed under the
[GNU Affero General Public License v3.0](LICENSE).

The JSON parser (`third_party/json.h`) is vendored from
[sheredom/json.h](https://github.com/sheredom/json.h) (Unlicense /
public domain) and keeps its own license.

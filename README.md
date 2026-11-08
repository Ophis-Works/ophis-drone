# ophis-drone

Three-chain drone synthesizer for Linux, C23 + SDL2 + ALSA. Runs as a desktop
application (640×480 UI at scale 2 → 1280×960 window) and natively on the
R36S handheld (RK3326, ArkOS, 640×480 fullscreen at scale 1). Audio:
44.1 kHz, float32, stereo, ALSA only; desktop runs are gated on a measured
xrun count of exactly 0. Engine and UI are separate threads communicating
only through lock-free channels (parameter mailbox, atomic event flags,
atomic status snapshot).

Each chain (A, B, C) has a main oscillator (sine/tri/saw/square/white/pink,
DUTY for square), two modulating sub-oscillators (free Hz or ratio-to-main;
destinations PITCH/PHASE/AMP/FILTER), ADSR, and an RBJ biquad filter
(LP/HP/BP/BR, 12 or 24 dB/oct, resonance, envelope amount). B and C sum to
the master bus and can cross-modulate A (3-slot matrix). After the mixer
(sum + tanh soft-clip): stereo delay → reverb → safety filter (HP 60 Hz +
LP 18 kHz cascaded, ON by default) → master level → master gate. Three
sequencers (one per chain, on/off as a UI parameter) send Hz + velocity
steps over 8 fixed steps; modes fwd / pingpong / oddeven / random / markov,
with loop or once (one pass then silent until restarted), clock divider
1–16 per chain (step period = 60/BPM/4 × div) and one GLOBAL BPM
1–500 shared by all sequencers, visible and edited on the MASTER tab. All
pitch parameters are
frequencies in Hz — no notes, no scales.

## Usage

Tabs: `OSC 1 | OSC 2 | OSC 3 | MOD | MASTER`; OSC tabs have sub-tabs
`OSC | ENV+FLT | SEQ` (MOD/MASTER have none). Every non-SEQ page is split
into labeled sections (gray label row, items indented, one 16 px gap row
between sections): the OSC page into MAIN/SUB 1/SUB 2, ENV+FLT into
ADSR/FILTER, MOD into MOD 1..3 and MASTER into MASTER/DELAY/REVERB.
MOD holds the 3-slot cross-mod matrix (each slot SRC/DEST/AMT, B/C
sources modulating chain A); MASTER holds the master level, the safety
filter toggle, the global BPM and the delay/reverb controls.

| Action | Desktop | R36S (proposed; verify per `CHECKLIST_R36S.md`) |
|---|---|---|
| Tab back / next | Q / E | L / R shoulder |
| Selection up / down | Up / Down | d-pad up / down |
| Value −/+ / cycle option | Z / X | Y / X |
| Navigation (sub-tabs, SEQ step rectangles) | Left / Right | d-pad left / right |
| TRIGGER (one-shot retrigger of all chains) | T | B |
| RUN (transport on/off) | Space | A |
| MENU (NEW/SAVE/LOAD/PANIC/EXIT/BACK) | Esc | Start |
| CONFIRM (menu select; SAVE name entry save) | Enter | A |

Inside the menu Enter confirms, Esc cancels/closes (the only cancel on
desktop; Space and T do nothing inside menus). Exit is menu-only. On the SEQ page LEFT/RIGHT selects the step rectangle from
anywhere on the page;
PITCH/VEL sliders edit that step live (vel 0 = silent step). SAVE name entry:
desktop types on the keyboard (Enter saves, Esc cancels); R36S uses a
character grid (d-pad select, X insert, Y delete, A save, B cancel). A hint
bar at the bottom of each screen shows the active keymap; the status strip at
the top shows the centered OPHIS DRONE title, RUN, gate, envelope bar and
live step cursor.

Presets are JSON (version 1) written to `presets/` next to the executable
(resolved at runtime from /proc/self/exe). A preset stores every parameter except
the RUN flag; required fields per `src/preset/schema.c` (over the
vendored `third_party/json.h`, see VENDORS below): `app`, `version`,
`master`, `safety`, `chain[3]` (`freq_hz`, `wave`, `duty`, `level`, `vel`,
`sub[2]`, `adsr`, `filter`), `seq[3]` (`on`, `bpm`, `div`, `mode`, `once`,
`steps[8]` with `pitch_hz`/`vel`), `xmod[3]` (`src`, `dest`, `amount`),
`delay` (`time_l`, `time_r`, `feedback`, `tone`, `mix`), `reverb` (`size`,
`damp`, `mix`, `width`). Fixed array lengths; unknown keys ignored; any other
error rejects the whole file.

## Installation

Desktop: run the built binary `build/desktop/ophis-drone`, or build (below).
Requires ALSA.

R36S/ArkOS: copy the contents of `build/deploy/ophis-drone-r36s.zip` to the
SD card so `ports/` lands at the `roms/` root — you get
`roms/ports/Ophis-Drone.sh` (launcher; keep it executable) and
`roms/ports/ophis-drone/ophis-drone`. The zip also ships the factory presets
(`ports/ophis-drone/presets/*.json`). Restart EmulationStation and launch
from the Ports menu. On-device acceptance checks are in
`CHECKLIST_R36S.md`.

## Building

Requirements — desktop: gcc, make, SDL2 and ALSA dev packages (pkg-config
finds `sdl2` and `alsa`). Handheld: `aarch64-linux-gnu-gcc` plus the arm64
SDL2/ALSA libraries via multiarch (`libsdl2-dev:arm64`,
`libasound2-dev:arm64` under `/usr/lib/aarch64-linux-gnu`), and `zip`.

```
make            # build/desktop/ophis-drone (-DTARGET_DESKTOP)
make test       # build + run all 16 unit test suites, prints TOTAL: N/M
make handheld   # build/r36s/ophis-drone + build/deploy/ophis-drone-r36s.zip
make clean
```

The handheld link meets the ArkOS glibc 2.28 target with symbol pinning
(`src/compat/glibc_symver.c`, `src/compat/crt0.s`), not static linking; the
`tools/handheld_gate.sh` gate (run by `make handheld`) fails unless the
binary is an aarch64 ELF requiring no glibc version above 2.28 (measured:
GLIBC_2.17 — see `BUILD_NOTES.md`).

## VENDORS

| Library | Source | Commit | License |
| ------- | ------ | ------ | ------- |
| `third_party/json.h` | https://github.com/sheredom/json.h | `2312d33bfd4e68a749fa7833353a78c21308ce8e` (master/main, vendored 2026-11-04, file verbatim) | Unlicense (public domain; header kept in the file) |

# BUILD_NOTES.md — M0 build decisions

## M6 REVISED sanctioned decision: host cross-build with glibc symbol pinning

The M0 static-link decision above is SUPERSEDED by the M6 owner ruling
(2026-11-02): build ON THE HOST with `aarch64-linux-gnu-gcc`, fully dynamic
link, and meet the ArkOS glibc-2.28 target with symbol-version pinning.

Mechanism (all in `src/compat/`, handheld link only; every other source file
is byte-identical between the two builds):
- `src/compat/glibc_symver.c` — `.symver` aliases pinning the only symbols
  the host toolchain bound too new: `pow`, `log2` (@GLIBC_2.29 →
  @GLIBC_2.17) and `pthread_create`, `pthread_join` (@GLIBC_2.34 →
  @GLIBC_2.17). Calls are redirected with `-Wl,--wrap=pow,--wrap=log2,
  --wrap=pthread_create,--wrap=pthread_join` (chosen over `-Dpow=...`
  because macro-renaming `pow`/`log2` breaks glibc's vector-math
  declarations in `math.h`).
- `src/compat/crt0.s` — the toolchain's `Scrt1.o` bakes
  `__libc_start_main@GLIBC_2.34`; linked with `-nostartfiles` and this
  minimal aarch64 `_start` that calls `__libc_start_main@GLIBC_2.17`
  (pinned via `.symver` in the .s itself).
- `libpthread.so.0` is forced into DT_NEEDED with `-Wl,--no-as-needed ...
  $(ARM64_LIB)/libpthread.so.0`: the host arm64 `libpthread.so.0` is an
  empty stub, so pthread symbols resolve from host libc at link time, but
  device glibc 2.28 keeps them in `libpthread.so.0` — it must be in the
  runtime search scope. Full link: `-lSDL2 -lasound -lm -lpthread -ldl`
  against the arm64 libs in `/usr/lib/aarch64-linux-gnu` (installed via
  multiarch: `libsdl2-dev:arm64`, `libasound2-dev:arm64`).

Gate (machine-checked, `tools/handheld_gate.sh`, run by `make handheld`):
`readelf -V` must show no required glibc version above GLIBC_2.28 and
`file` must say aarch64 ELF. Measured output:

```
$ make handheld
GATE OK: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), dy
GATE: max required version = GLIBC_2.17
GATE PASS: build/r36s/ophis-drone (aarch64, <= GLIBC_2.28)
HANDHELD OK: build/deploy/ophis-drone-r36s.zip
```

Extra runtime smoke (host, not the gate): the binary runs under
`qemu-aarch64 -L /usr/aarch64-linux-gnu LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu`
— all shared-object version resolution succeeds, SDL video comes up, and it
runs to a clean exit failing only on the host ALSA device ("default" →
missing pipewire module), i.e. `XRUNS -1` (ALSA never opened on host).
`DT_NEEDED`: libSDL2-2.0.so.0, libasound.so.2, libm.so.6, libpthread.so.0,
libc.so.6 — all present in ArkOS userspace.

Deploy packaging (PLAN.md §11 M6): `make handheld` produces
`build/deploy/ports/ophis-drone/ophis-drone` (binary) plus the executable
launcher `build/deploy/ports/Ophis-Drone.sh` OUTSIDE the program folder
(it `cd "$(dirname "$0")"` and `exec ./ophis-drone/ophis-drone`; template
checked in as `tools/r36s-launcher.sh`), zipped to
`build/deploy/ophis-drone-r36s.zip` (user copies `ports/` into the SD
`roms/` root). `build/deploy/` is gitignored.

## Static-link vs container for the R36S build (M0 sanctioned decision — SUPERSEDED, kept for the record)

**Decision: static linking of SDL2/ALSA for the R36S (`make handheld`) build
is the sanctioned M0 choice.** Not a container build.

Reasoning:
- ArkOS lineage is Ubuntu 19.10 userspace with glibc 2.28 (PLAN.md §1). The
  host cross toolchain is aarch64-linux-gnu-gcc 15.2.0 (Ubuntu 15.2.0-16ubuntu1,
  verified below) against a much newer host glibc; a dynamically linked
  binary would fail on-device with a glibc-version error.
- A 19.10-container build path needs network image pulls and more moving
  parts for a 1 GB-device target; static linking removes the entire runtime
  library-version class of failure at once (glibc, libasound, SDL2).
- No hard blocker found in M0: the toolchain is installed, and the only
  caveat is that static aarch64 SDL2/ALSA archives must be present at
  cross-build time (M6) — e.g. `libsdl2-dev:arm64` + `libasound2-dev:arm64`
  via multiarch, or vendor-built archives placed under `build/r36s/`. That
  is a build-time packaging concern for M6, not a blocker to the decision.
- Static binaries run from anywhere on the SD card — matching the no-WiFi
  deploy path (SD card / USB OTG, PLAN.md §1).

Recorded by M0 per PLAN.md §11 (decision delegated to M0 by PLAN.md §12.1).

## Dependency check output (measured 2026-11-01)

```
$ pkg-config --modversion sdl2 alsa
2.32.10
1.2.15.3
$ aarch64-linux-gnu-gcc --version | head -1
aarch64-linux-gnu-gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
```

## Open notes for M5/M6

- Desktop L/R page keys are Q/E in `src/config.h` (user-set decision).
- R36S KB_* values in config.h are PROPOSED gamepad-button numbers; verify
  against the physical device per CHECKLIST_R36S.md before M6.

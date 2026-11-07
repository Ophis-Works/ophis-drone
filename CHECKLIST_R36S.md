# CHECKLIST_R36S.md — on-device acceptance checklist (M6)

Build: `make handheld` → deploy artifact
`build/deploy/ophis-drone-r36s.zip` (machine gate already passed on the
host: aarch64 ELF, max required glibc version GLIBC_2.17 ≤ 2.28 — see
BUILD_NOTES.md). Copy the zip to the R36S via SD card or USB OTG, unzip at
the SD `roms/` root so you get `roms/ports/Ophis-Drone.sh` and
`roms/ports/ophis-drone/ophis-drone`, then restart EmulationStation and
answer these on the device:

1. **Ports menu visibility**: does "Ophis Drone" appear in EmulationStation's
   Ports section? (It lists `roms/ports/Ophis-Drone.sh`; the launcher must
   be executable — re-`chmod +x` if the zip transfer stripped the bit.)
2. **Launch**: does the launcher start the binary (it cds to its own
   directory and `exec ./ophis-drone/ophis-drone`)? If it fails, run the
   launcher from a shell (`ssh`/`Enable Remote Services`) and paste the
   error — a glibc/SDL "version not found" error must NOT appear (if it
   does, report the exact message).
3. **Video driver**: which SDL video driver does the device pick
   (KMSDRM / fbcon / other)? Does the 640×480 window appear and fill the
   screen (fullscreen, UI_SCALE=1)?
4. **Audio device**: `aplay -l` output — what is the default ALSA device,
   and is there sound? On exit the app prints `XRUNS <n>` (last line of
   output) — paste it; xruns must be 0 during a multi-minute run.
5. **Button layout** (config.h R36S bindings): A=RUN/STOP, B=TRIGGER/RETRIG,
    Start=MENU (Select = second MENU key), d-pad selection + navigation
    (Y=value −, X=value +), L/R pages. Does the d-pad reach the app, and
    do A/B/Start map as proposed? The app loads the OS controller
    databases (/opt/system/Tools/PortMaster and /opt/inttools) after its
    bundled fallback, so the device-correct mapping wins; the launcher
    captures stderr in ophis-drone.err beside the binary.
6. **Preset save/load on device**: MENU → SAVE (grid name entry) → NEW →
   LOAD. Is `presets/` beside the executable created and writable? Do saved
   presets persist across a reboot?
7. **Stability**: run for several minutes — stable frame rate, no xruns,
   clean exit via MENU → EXIT?

Manual checks only — none of 1–7 is machine-verified; the machine-verified
part is the host gate in `tools/handheld_gate.sh` (run by `make handheld`).

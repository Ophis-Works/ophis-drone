# LOG_R36S_START_DIAG.md — implementation log. Plan: PLAN_R36S_START_DIAG.md.

## M1 — controller.log diagnostics

- main.c (R36S-gated only): controller.log opened BEFORE the mapping
  block (logs mapping count / rejects / not-found), then per-pad
  name + GUID (SDL_JoystickGetDeviceGUID) + open result, then the first
  64 button-downs as `JOY raw=<n>` / `CTL logical=<n>` with fflush per
  line. Overwritten each launch. No allocation, no locks; globals
  g_ctllog/g_ctllog_n; desktop path compiles out (verified: no
  controller.log after the desktop smoke run).
- Decision recorded: NO speculative code fix for Start — the log
  discriminates the three hypotheses (raw bN mismatch / OS intercept /
  our code) in one device round-trip (PLAN_R36S_START_DIAG.md). The
  actual fix (mapping bN edit, or a KB_MENU rebind decision) follows the
  owner's log paste.
- "ENV" in the top strip: answered as by-design (live envelope bar,
  PLAN §8.6) — no change made; relocation/removal is the owner's call.
- Verification: desktop relink + capped smoke XRUNS 0 (no log file
  created); handheld build + gate OK; zip rebuilt (ships the log
  capability).

## M2 — device crash mitigation (owner report: any button kills the app)

- Owner device report: with the per-event-fflush build (83d3d5a zip),
  pressing ANY button after launch kills the program. The per-event
  fprintf+fflush to the SD card from the event thread is the only new
  code on that path — removed regardless of the exact device mechanism:
  events now buffer in RAM (fixed int arrays, 64 cap, NO file I/O in
  the event loop); one flush after the FIRST captured press lands the
  dead-button fact on the card early; remaining events are written and
  the file closed on the normal exit path (ctllog_close()).
- launcher (tools/r36s-launcher.sh): app output now teed to
  /tmp/ophis-drone.err (tmpfs, no SD wear) — launch errors, mapping
  lines and any crash message survive until reboot for the owner to
  read. (exec dropped: tee needs the pipeline.)
- LINK REGRESSION caught and fixed in the same pass: atexit() pulls
  libc_nonshared.a(atexit.oS) which needs __dso_handle — undefined
  under the custom -nostartfiles crt0 (aarch64 link failed). Replaced
  with the explicit ctllog_close() call. NOTE: the earlier `make
  handheld | tail && git commit` masked the failure (pipe exit status)
  — the broken intermediate was committed as cf6a47b; this fix is
  committed on top after a verified link.
- Verification: handheld link + gate OK (exit status checked directly);
  desktop relink + capped smoke XRUNS 0; full suite 98881/98881.

## M3 — launcher orphan fix (owner report: cannot exit the app)

- Owner device report: with the tee pipeline launcher (cf6a47b), the app
  could not be killed from EmulationStation. Cause: exec previously made
  the binary ES's direct child (killing the port kills the app); the
  pipeline made ES kill the SHELL instead, orphaning binary + tee.
- FIX: back to exec with redirection — `exec ./ophis-drone
  2> /tmp/ophis-drone.err` — direct-child semantics restored, stderr
  capture kept (tmpfs file). Comment in the launcher forbids pipelines
  so this cannot regress silently.
- Verified: sh -n syntax OK; handheld rebuild + gate OK (exit status
  direct); the shipped zip's ports/Ophis-Drone.sh contains the exec
  line (unzip -p checked).

## M4 — exit path without Start (owner demand)

- Select (logical BACK = 4) is now KB_MENU2, a second MENU key on the
  R36S: opens AND closes menus exactly like Start, so MENU → EXIT works
  without Start. Desktop: KB_MENU2 = -1 (dead case label, Select
  unbound). map_code: `case KB_MENU2: ACT_MENU`.
- TDD: red = KB_MENU2 undeclared; pins — test_keymap_r36s KB_MENU2==4,
  test_keymap_events_r36s cbutton(4) → ACT_MENU (flipped from the
  "BACK unbound" pin), test_keymap.c desktop KB_MENU2==-1.
- Verification: suite 98884/98884; desktop smoke XRUNS 0; handheld
  link + gate OK (exit status direct); shipped zip checked for the exec
  launcher line. Interim until controller.log names Start's raw button;
  kept as a convenience afterwards unless the owner says otherwise.

## M5 — ROOT CAUSE FOUND: our mapping stompED the OS's correct one

- Owner demand to re-audit + research (2026-11-05) proved it: our
  bundled gamecontrollerdb.txt line is the RG351 "GO-Advance rev 1.1"
  layout (back:b12, start:b17) — copied from a reference and NEVER
  device-verified. The R36S community layout differs completely (e.g.
  back:b8, start:b9, dpad b13–b16 — dov/r36s-programming,
  Winlator-RK3326-dArkOS notes), and ArkOS ships the device-correct
  database (/opt/system/Tools/PortMaster/gamecontrollerdb.txt,
  /opt/inttools/gamecontrollerdb.txt).
- The bug in OUR code: SDL applies the OS config at init
  (SDL_GAMECONTROLLERCONFIG_FILE), and then our main.c called
  SDL_GameControllerAddMapping with OUR line for the SAME GUID —
  replacing the device-correct mapping. Buttons whose raw numbers
  coincide kept working; Start and Select died. The owner was right:
  our code broke it.
- FIX: main.c now loads the OS databases AFTER ours (same-GUID
  replacement semantics) — /opt/system/Tools/PortMaster/
  gamecontrollerdb.txt then /opt/inttools/gamecontrollerdb.txt — so the
  device-correct mapping always wins; ours is fallback only. Load
  counts logged to controller.log + stderr.
- Triggers in our fallback line also corrected to the PortMaster
  reference (b6/b7, was b14/b15) in 22c3db4.
- Verification: desktop relink + capped smoke XRUNS 0; handheld link +
  gate OK (exit status direct); zip rebuilt.
- Sources: christianhaitian/PortMaster packaging.md; dov/r36s-programming;
  davizerascript/Winlator-RK3326-dArkOS; mdqinc/SDL_GameControllerDB.

## M6 — bundled controller db removed (owner order)

- Owner order 2026-11-05: no bundled fallback mapping lines at all.
  gamecontrollerdb.txt deleted; main.c loads ONLY the OS databases
  (/opt/system/Tools/PortMaster, /opt/inttools); Makefile drops the
  staging prerequisite + copy; zip recipe purges a stale staged copy
  (F13 lesson — an upgrade over an older deploy dir kept shipping it);
  launcher comment updated; checklist item 5 already describes the
  OS-database behavior.
- Consequence (accepted by the order): on a system without those OS
  files the pad has no mapping — SDL's built-in db may still apply.
- Verification: zip lists 11 files, no gamecontrollerdb; suite
  98884/98884; desktop smoke XRUNS 0; handheld gate OK.

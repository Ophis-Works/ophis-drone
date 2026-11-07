#!/bin/sh
# tools/r36s-launcher.sh — template for ports/Ophis-Drone.sh (PLAN.md §11 M6):
# EmulationStation lists this launcher in roms/ports/; it must run the
# binary that lives in the program folder next to it. We cd into the
# program folder (which also contains presets/ after first save); the
# binary loads the OS controller databases from /opt.
# The app's stderr is redirected to ophis-drone.err IN THE PROGRAM
# FOLDER (on the SD card — readable with a card reader, no SSH needed):
# launch errors, the controller-mapping lines and any crash message are
# overwritten on each launch. exec KEEPS the binary as the direct child
# of EmulationStation — killing the port from ES must kill the app, so
# no pipelines here (a pipeline orphans the binary).
cd "$(dirname "$0")/ophis-drone" || exit 1
exec ./ophis-drone 2> ophis-drone.err

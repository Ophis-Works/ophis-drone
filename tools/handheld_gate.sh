#!/bin/sh
# tools/handheld_gate.sh — machine-checked handheld gate:
#   1. binary must be an aarch64 ELF;
#   2. no required glibc version in .gnu.version_r may exceed GLIBC_2.28
#      (ArkOS lineage is Ubuntu 19.10 / glibc 2.28).
# Usage: tools/handheld_gate.sh <binary> ; exit 0 = pass, 1 = fail.
set -e

BIN="${1:?usage: handheld_gate.sh <binary>}"

[ -f "$BIN" ] || { echo "GATE FAIL: $BIN does not exist"; exit 1; }

if ! file "$BIN" | grep -q "ARM aarch64"; then
    echo "GATE FAIL: $BIN is not an aarch64 ELF:"
    file "$BIN"
    exit 1
fi
echo "GATE OK: $(file -b "$BIN" | cut -c1-60)"

# Highest required GLIBC version (readelf output forced to C locale — the
# lines look like: '  0x0050:   Name: GLIBC_2.29  Flags: none  Version: 7').
MAX=`LC_ALL=C readelf -W -V "$BIN" | awk '
    /Name: GLIBC_2\.[0-9]+/ {
        for (i = 1; i <= NF; i++)
            if ($i ~ /^GLIBC_2\.[0-9]+$/) {
                v = substr($i, 9) + 0
                if (v > max) max = v
            }
    }
    END { print max + 0 }
'`

echo "GATE: max required version = GLIBC_2.${MAX}"
if [ "${MAX}" -gt 28 ]; then
    echo "GATE FAIL: required glibc version exceeds GLIBC_2.28"
    LC_ALL=C readelf -W -V "$BIN" | grep "Name: GLIBC_2"
    exit 1
fi
echo "GATE PASS: $BIN (aarch64, <= GLIBC_2.28)"

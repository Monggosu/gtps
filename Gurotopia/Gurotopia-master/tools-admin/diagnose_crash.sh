#!/bin/bash
# Turns crash_report.txt's raw stack trace into exact file:line locations.
# Needs the SAME main.out that produced the crash (don't rebuild in between).
cd "$(dirname "$0")/.."

[ -f crash_report.txt ] || { echo "No crash_report.txt found."; exit 1; }
[ -f main.out ] || { echo "main.out not found here."; exit 1; }

echo "---- crash_report.txt ----"
cat crash_report.txt
echo "---- exact locations ----"

# crash_report.txt lines look like:  ./main.out(_ZN3foo3barEv+0x12) [0x55a1b2c3d4e5]
# pull out the address in [...] from each line and resolve it with addr2line.
grep -oE '\[0x[0-9a-f]+\]' crash_report.txt | tr -d '[]' | while read -r addr; do
    addr2line -e main.out -f -C -p "$addr" 2>/dev/null
done
echo "--------------------------"
echo "(if this says '?? ??:0' for every line, rebuild with 'make clean && make -j\$(nproc)' first - the binary needs -g debug info, which the Makefile already enables)"

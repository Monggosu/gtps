#!/bin/bash
# Keeps the server alive: if it crashes, it prints EXACTLY where (file:line) it
# crashed, right here in this log, then starts it again after a few seconds.
# No manual step needed: this is read automatically every time.
cd "$(dirname "$0")"

while true; do
    rm -f crash_report.txt
    ./main.out
    code=$?

    if [ $code -eq 0 ] || [ $code -eq 130 ] || [ $code -eq 143 ]; then
        echo "server stopped normally (exit $code)"
        break
    fi

    echo
    echo "================ SERVER CRASHED (exit $code) at $(date) ================"
    if [ -f crash_report.txt ] && [ -x tools-admin/diagnose_crash.sh ]; then
        ./tools-admin/diagnose_crash.sh
    else
        echo "(no crash_report.txt - this crash happened before Sentinel's handler was installed, or the binary is missing debug info)"
    fi
    echo "==========================================================================="
    echo
    echo "starting again in 3 seconds..."
    sleep 3
done

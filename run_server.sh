#!/bin/bash
# Keeps the server alive: if it crashes it is started again after a few seconds.
# Sentinel writes crash_report.txt when the server dies and reads it on the next start (see /sentinel).
cd "$(dirname "$0")"
while true; do
    ./main.out
    code=$?
    if [ $code -eq 0 ] || [ $code -eq 130 ] || [ $code -eq 143 ]; then
        echo "server stopped normally (exit $code)"
        break
    fi
    echo "server crashed (exit $code) at $(date), starting again in 3 seconds..."
    sleep 3
done

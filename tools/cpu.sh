#!/bin/bash
# Steady-state CPU cost of the standalone.
#
# `ps -o %cpu` is a decaying LIFETIME average on macOS: it keeps climbing for
# the first minute of a process's life, so two builds sampled at different ages
# cannot be compared with it. This samples cumulative CPU time twice and
# reports the delta over the wall time between them, after a warm-up.
PID=$(pgrep -f "KeepThat.app/Contents/MacOS/KeepThat" | head -1)
[ -z "$PID" ] && { echo "not running"; exit 1; }
cpusec() { ps -o cputime= -p "$1" | tr -d ' ' | awk -F: '{n=NF; s=$n; if(n>1) s+=$(n-1)*60; if(n>2) s+=$(n-2)*3600; print s}'; }
WARM=${1:-20}; WIN=${2:-20}
sleep "$WARM"
A=$(cpusec $PID); T0=$(date +%s)
sleep "$WIN"
B=$(cpusec $PID); T1=$(date +%s)
awk -v a="$A" -v b="$B" -v t0="$T0" -v t1="$T1" \
    'BEGIN { printf "  %.1f%% of one core (%.2f CPU-s over %d s)\n", (b-a)/(t1-t0)*100, b-a, t1-t0 }'

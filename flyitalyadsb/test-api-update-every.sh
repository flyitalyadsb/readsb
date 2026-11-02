#!/bin/bash
# flyitalyadsb: --api-update-every keeps the API (and the map, through
# --tar1090-use-api) fresher than the json files, which keep --write-json-every.
#
#   flyitalyadsb/test-api-update-every.sh [path/to/readsb]   (default ./readsb)
#
# Starts readsb with no input (net-only), samples the API's "now" and
# aircraft.json's "now" ~every 0.1 s for 8 s, and takes the median gap between
# distinct values: readsb's own timestamps of each update, so the sampling's
# speed doesn't matter. Also checks that without the option nothing changes.
set -euo pipefail
BIN=$(realpath "${1:-./readsb}")
WORK=$(mktemp -d)
PID=
trap '[ -n "$PID" ] && kill "$PID" 2>/dev/null; wait 2>/dev/null; rm -rf "$WORK"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# run NAME ARGS...: start readsb in $WORK/NAME, wait for receiver.json and the API socket
run() {
    local d="$WORK/$1"; shift
    mkdir -p "$d"
    "$BIN" --net-only --quiet --net-ri-port=0 --net-ro-port=0 --net-sbs-port=0 --net-bi-port=0 --net-bo-port=0 \
        --write-json="$d" --net-api-port="unix:$d/api.sock" --tar1090-use-api "$@" > "$d/log" 2>&1 &
    PID=$!
    for _ in $(seq 100); do
        [ -S "$d/api.sock" ] && [ -s "$d/receiver.json" ] && [ -s "$d/aircraft.json" ] && return 0
        sleep 0.1
    done
    cat "$d/log"; fail "$1: readsb didn't start"
}
stop() { kill "$PID"; wait "$PID" 2>/dev/null || true; PID=; }
now_of() { python3 -c 'import json,sys; print(json.load(sys.stdin)["now"])'; }
refresh() { python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["refresh"])' "$WORK/$1/receiver.json"; }

# sample NAME: prints "<API gap> <aircraft.json gap>", the median seconds between
# distinct "now" values seen over 8 s
sample() {
    local d="$WORK/$1" end=$((SECONDS + 8))
    : > "$d/api.now"; : > "$d/file.now"
    while [ "$SECONDS" -lt "$end" ]; do
        curl -s --unix-socket "$d/api.sock" 'http://localhost/?all' | now_of >> "$d/api.now"
        now_of < "$d/aircraft.json" >> "$d/file.now" 2>/dev/null || true
        sleep 0.1
    done
    python3 - "$d/api.now" "$d/file.now" <<'PY'
import statistics, sys
def gap(path):
    v = sorted({float(x) for x in open(path).read().split()})
    return statistics.median(b - a for a, b in zip(v, v[1:])) if len(v) > 2 else 0
print("%.2f %.2f" % (gap(sys.argv[1]), gap(sys.argv[2])))
PY
}
# between LOW HIGH VALUE
between() { python3 -c 'import sys; lo, hi, v = map(float, sys.argv[1:]); sys.exit(not lo <= v <= hi)' "$@"; }

# 1. API every 0.5 s, files every 2 s
run fast --write-json-every=2 --api-update-every=0.5
[ "$(refresh fast)" = 500 ] || fail "receiver.json refresh is $(refresh fast), expected 500"
read -r api file < <(sample fast)
echo "api-update-every 0.5, write-json-every 2: API every $api s, aircraft.json every $file s"
between 0.4 0.6 "$api" || fail "API updated every $api s, expected 0.5"
between 1.8 2.2 "$file" || fail "aircraft.json written every $file s, expected 2"
stop

# 2. Without the option: as upstream, both every 2 s
run same --write-json-every=2
[ "$(refresh same)" = 2000 ] || fail "receiver.json refresh is $(refresh same), expected 2000"
read -r api file < <(sample same)
echo "write-json-every 2 only: API every $api s, aircraft.json every $file s"
between 1.8 2.2 "$api" || fail "API updated every $api s, expected 2"
between 1.8 2.2 "$file" || fail "aircraft.json written every $file s, expected 2"
stop

# 3. Longer than write-json-every: clamped, with a warning
run slow --write-json-every=2 --api-update-every=5
[ "$(refresh slow)" = 2000 ] || fail "receiver.json refresh is $(refresh slow), expected 2000 (clamped)"
grep -q "api-update-every can't be longer" "$WORK/slow/log" || fail "no warning for --api-update-every 5 > 2"
stop

echo "ok"

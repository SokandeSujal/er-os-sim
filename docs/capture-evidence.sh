#!/bin/bash
# Rebuild the report's terminal evidence images from actual simulator/demo output.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
mkdir -p docs/screenshots
tmp=$(mktemp -d)
cleanup() { jobs -pr | xargs -r kill 2>/dev/null || true; rm -rf "$tmp"; }
trap cleanup EXIT

./er_sim --scenario scenarios/classic.csv --policy srtf --no-ui --fast > "$tmp/srtf"
./er_sim --scenario scenarios/classic.csv --policy fcfs --no-ui --fast > "$tmp/fcfs"
{
    echo 'Classic scheduling comparison'
    echo 'Policy  Average TAT  Average WT'
    awk '/Average TAT/{tat=$3}/Average WT/{print "SRTF   " tat "       " $3}' "$tmp/srtf"
    awk '/Average TAT/{tat=$3}/Average WT/{print "FCFS   " tat "       " $3}' "$tmp/fcfs"
} > "$tmp/policy.txt"

./er_sim --scenario scenarios/er_rush.csv --no-ui --fast > "$tmp/sync"
./er_sim --scenario scenarios/er_rush.csv --no-ui --fast --no-sync > "$tmp/nosync"
{
    echo 'Readers-writers invariant: a + b == sum'
    printf 'Synchronized:   '; grep 'Records: reads=' "$tmp/sync" | tail -1
    printf 'No synchronization: '; grep 'Records: reads=' "$tmp/nosync" | tail -1
} > "$tmp/corruption.txt"

script -q -c "timeout --signal=INT 1 ./er_sim --scenario scenarios/er_rush.csv" "$tmp/dashboard.raw" >/dev/null || true
perl -pe 's/\e\[[0-9;?]*[A-Za-z]//g; s/\r//g' "$tmp/dashboard.raw" | tail -n 34 > "$tmp/dashboard.txt"

demos/zombie > "$tmp/zombie.log" 2>&1 &
zparent=$!
sleep 1
{
    cat "$tmp/zombie.log"
    ps -o pid,ppid,state,cmd --ppid "$zparent"
} > "$tmp/zombie.txt"
kill "$zparent" 2>/dev/null || true
wait "$zparent" 2>/dev/null || true

render() {
    local text
    text=$(cat "$1")
    convert -background '#101820' -fill '#f2f4f8' -font DejaVu-Sans-Mono \
        -pointsize 16 -bordercolor '#101820' -border 24x24 "label:$text" "$2"
}
render "$tmp/dashboard.txt" docs/screenshots/dashboard.png
render "$tmp/policy.txt" docs/screenshots/policy-comparison.png
render "$tmp/corruption.txt" docs/screenshots/corruption-demo.png
render "$tmp/zombie.txt" docs/screenshots/zombie-ps.png
echo 'Evidence screenshots regenerated.'

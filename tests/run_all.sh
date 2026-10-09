#!/bin/sh
# Full host test battery. Usage: tests/run_all.sh [outdir]
#  - sanitizer build (ASan + UBSan): fuzz, AI matches, cups, 2 players, panic, ghosting, screenshots
#  - fast build: ability balance matrix and Hard-vs-Normal head to head
cd "$(dirname "$0")"
OUT=${1:-/tmp/fbl_tests}
mkdir -p "$OUT/shots"
./build.sh || exit 1
./build_fast.sh || exit 1
run() { name=$1; shift; ( "$@" > "$OUT/$name.txt" 2>&1; echo "exit=$?" >> "$OUT/$name.txt" ) & }
run ghost       ./harness ghost
run panic       ./harness panic
run tour        ./harness tour 6 2
run aivai       ./harness aivai 24 1
run fuzz7       ./harness fuzz 80000 7
run fuzz8       ./harness fuzz 80000 8
run friendly    ./harness friendly 3 3
run shots       ./harness shots "$OUT/shots" 9
wait
PER=50 ./harness_fast balance 4500 23 > "$OUT/balance.txt" 2>&1; echo "exit=$?" >> "$OUT/balance.txt"
P1DIFF=1 NOSWITCH=1 ./harness_fast aivai 400 41 > "$OUT/hard_left.txt" 2>&1
P1DIFF=0 HARD=1 ./harness_fast aivai 400 44 > "$OUT/hard_right.txt" 2>&1
for f in "$OUT"/*.txt; do
  echo "=== $(basename "$f" .txt)"
  grep -v "^WATCH\|^panic target" "$f" | grep "ghost check\|number-pad\|panic exits\|scenario=\|matches=\|avg match\|rests\|tours=\|restore pixel\|EST\.\|exit=\|ERROR\|FAILED"
done

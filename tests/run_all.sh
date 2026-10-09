#!/bin/sh
# Full host test battery (ASan + UBSan build). Usage: tests/run_all.sh [outdir]
cd "$(dirname "$0")"
OUT=${1:-/tmp/fbl_tests}
mkdir -p "$OUT/shots"
./build.sh || exit 1
run() { name=$1; shift; ( "$@" > "$OUT/$name.txt" 2>&1; echo "exit=$?" >> "$OUT/$name.txt" ) & }
run tour        ./harness tour 8 2
run aivai       ./harness aivai 40 1
run fuzz7       ./harness fuzz 150000 7
run fuzz8       ./harness fuzz 150000 8
run fuzz9       ./harness fuzz 150000 9
run friendly    ./harness friendly 2 3
run shots       ./harness shots "$OUT/shots" 9
wait
for f in "$OUT"/*.txt; do
  echo "=== $(basename "$f" .txt)"
  grep -v "^WATCH" "$f" | grep "scenario=\|matches=\|avg match\|tours=\|restore pixel\|EST\.\|histogram\|exit=\|ERROR"
done

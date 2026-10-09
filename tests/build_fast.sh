#!/bin/sh
# Optimised build without sanitizers, for long balance runs.
set -e
cd "$(dirname "$0")"
CF="-O2 -w"
gcc $CF -DFBL_HOST_TEST -Dmain=game_main -Dclock=fake_clock -Istub -c ../FBLEGEND/src/main.c -o main_fast.o
gcc $CF -Istub -c stub/stub.c -o stub_fast.o
gcc $CF -c harness.c -o harness_fast.o
gcc $CF main_fast.o stub_fast.o harness_fast.o -o harness_fast
echo built tests/harness_fast

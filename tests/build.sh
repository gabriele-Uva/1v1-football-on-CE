#!/bin/sh
# Builds the host test harness (ASan + UBSan) from the real game source.
set -e
cd "$(dirname "$0")"
CFLAGS="-g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer"
gcc $CFLAGS -DFBL_HOST_TEST -Dmain=game_main -Dclock=fake_clock -Istub -c ../FBLEGEND/src/main.c -o main_host.o
gcc $CFLAGS -Istub -c stub/stub.c -o stub.o
gcc $CFLAGS -c harness.c -o harness.o
gcc $CFLAGS -rdynamic main_host.o stub.o harness.o -o harness
echo built tests/harness

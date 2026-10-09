# Football Legends CE

A 2D side-view "head soccer" game for the **TI-84 Plus CE**, written in C for the
[CE C/C++ Toolchain (CEdev)](https://ce-programming.github.io/toolchain/), styled after
Football Legends: stadium with roof and crowd, goals on raised platforms, 10 legends with
unique abilities, 1-player and 2-player modes.

| Path | What |
|---|---|
| `FBLEGEND/src/main.c` | the whole game |
| `FBLEGEND/makefile` | CEdev makefile |
| `FBLEGEND/README.txt` | controls, features, build steps, test results, honest limits |
| `fblegend_main_c.txt`, `fblegend_makefile.txt`, `fblegend_README.txt` | plain-text copies |
| `prebuilt/FBLEGEND.8xp` | ready-built program (CEdev v13.0) |
| `docs/screenshots/` | screenshots from the PC test build |
| `tests/` | PC test harness (stub headers, fake framebuffer, keypad ghosting, ASan/UBSan) |

Build: open the CEdev prompt in `FBLEGEND/` and run `make`.
PC tests (Linux, gcc): `tests/run_all.sh`.

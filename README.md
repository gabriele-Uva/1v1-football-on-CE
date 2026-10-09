# Football Legends CE

A 2D side-view "head soccer" game for the **TI-84 Plus CE**, written in C for the
[CE C/C++ Toolchain (CEdev)](https://ce-programming.github.io/toolchain/).

| Path | What |
|---|---|
| `FBLEGEND/src/main.c` | the whole game |
| `FBLEGEND/makefile` | CEdev makefile |
| `FBLEGEND/README.txt` | controls, features, build steps, honest limits |
| `fblegend_main_c.txt`, `fblegend_makefile.txt`, `fblegend_README.txt` | plain-text copies of the three files above |
| `prebuilt/FBLEGEND.8xp` | ready-built program (CEdev v13.0) |
| `docs/screenshots/` | screenshots from the PC test build |
| `tests/` | PC test harness (stub headers, fake framebuffer, ASan/UBSan) |

Build: open the CEdev prompt in `FBLEGEND/` and run `make`. See `FBLEGEND/README.txt`.

Run the PC tests (Linux, gcc): `tests/run_all.sh`.

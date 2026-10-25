# Contributing

- `nome.c` is the whole tool; keep it small, plain C11/GNU C, no dependencies.
- `emuroot/` is a vendored copy of [emuroot](https://github.com/mkalmousli/emuroot); change it upstream.
- Build and install with `./build.c --prefix /tmp/p`; try `./build.c --help`.
- Update `man/nome.1`, the README and `nome --help` together when you add a command.
- By contributing you agree your work is licensed GPL-3.0-or-later.

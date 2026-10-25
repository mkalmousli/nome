# nome

**Switch between Linux distros and their user data — no root, no containers, one static binary.**

`nome` gives every distro its own *profile*: a root filesystem plus a private `/root` home.
Pick one in a colorful TUI, or run commands straight from your shell. It embeds
[emuroot](https://github.com/mkalmousli/emuroot), so there is nothing else to install.

```sh
nome get ubuntu mo      # download Ubuntu 24.04 into profile "mo"
nome mo                 # open bash inside it, as (fake) root
nome mo ls /            # run any command inside it
nome                    # guided TUI
```

**[Watch the demo](https://mkalmousli.github.io/nome/)** (real asciinema recording) · `man nome` · `nome --help`

## Install

```sh
git clone --depth 1 https://github.com/mkalmousli/nome && cd nome && ./build.c
```

Installs one static binary to `~/.local/bin/nome` and the man page (`man nome`); as root it uses `/usr/local`.
Change with `./build.c --prefix DIR`; remove with `./build.c uninstall`; see `./build.c --help`.
Needs only a C compiler; downloading distros needs `curl`, `tar`, `xz`.

Prebuilt static binaries for Linux (x86_64, aarch64, armv7, i386, s390x) are attached to every
[release](https://github.com/mkalmousli/nome/releases) as `nome-linux-<arch>`.

**Updating:** `nome` checks for a new release at most once a day (interactive use only, silent offline;
`NOME_NO_UPDATE_CHECK=1` disables it) and tells you; `nome update` swaps in the new binary for your architecture.

## Usage

| Command | Does |
|---|---|
| `nome` | guided TUI: enter, install, add, delete profiles |
| `nome distros` | list ready-to-use distros |
| `nome get DISTRO [NAME]` | download a distro into profile `NAME` (default: the distro key) |
| `nome add NAME ROOTFS` | register your own extracted root filesystem |
| `nome NAME [CMD...]` | run `CMD` in the profile (default: `bash`, or `sh`) |
| `nome cp OLD NEW` / `nome mv OLD NEW` | duplicate (rootfs and data) / rename a profile |
| `nome mount NAME HOST[:GUEST]` | share a host folder (default guest path `/mnt/<folder>`) |
| `nome umount NAME PATH` / `nome mounts NAME` | stop sharing (by host or guest path) / list shares |
| `nome ls` / `nome rm NAME` | list / delete profiles |
| `nome update` | upgrade to the latest release |

Ready-to-use distros (from [images.linuxcontainers.org](https://images.linuxcontainers.org), x86_64 and aarch64):
`alpine` `ubuntu` `ubuntu22` `debian` `arch` `fedora` `rocky` `opensuse` `kali` `void`.
Alpine, Ubuntu, Debian, Arch and Fedora have been verified; the others are untested.

## Profiles

```
~/.nome/NAME/
├── rootfs/   the distro (or a symlink to your own, from `nome add`)
├── home/     user data, mounted as /root — survives reinstalling the distro
├── own       fake file ownership, remembered between runs
└── binds     extra bind mounts, one HOST:GUEST per line (managed by `nome mount`)
```

`nome` binds `/proc`, `/dev` and `/etc/resolv.conf` automatically. Set `NO_COLOR=1` to disable colors.

## Contributing

Issues and PRs welcome, see [CONTRIBUTING.md](CONTRIBUTING.md). The demo is reproducible:
`HOME=$(mktemp -d) asciinema rec -c docs/demo.sh docs/demo.cast`.

## Build

`build.c` is the only build system and runs directly (no argument = build and install): `./build.c [build|uninstall|clean]`
(it picks a compiler, falls back to dynamic linking if static fails, warns if the bin dir is not in `PATH`,
and supports `--prefix` / `--destdir` for packagers).
`emuroot/` is a vendored copy of emuroot, compiled into the same binary.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Because emuroot (GPL-3.0) is compiled in, `nome` is GPL-3.0 too.

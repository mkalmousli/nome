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

## Install

```sh
git clone --depth 1 https://github.com/mkalmousli/nome && cd nome && cc build.c -o build && ./build install
```

Installs one static binary to `~/.local/bin/nome` (`PREFIX=/usr/local ./build install` to change).
Remove with `./build uninstall`. Needs only a C compiler; downloading distros needs `curl`, `tar`, `xz`.

## Usage

| Command | Does |
|---|---|
| `nome` | guided TUI: enter, install, add, delete profiles |
| `nome distros` | list ready-to-use distros |
| `nome get DISTRO [NAME]` | download a distro into profile `NAME` (default: the distro key) |
| `nome add NAME ROOTFS` | register your own extracted root filesystem |
| `nome NAME [CMD...]` | run `CMD` in the profile (default: `bash`, or `sh`) |
| `nome ls` / `nome rm NAME` | list / delete profiles |

Ready-to-use distros (from [images.linuxcontainers.org](https://images.linuxcontainers.org), x86_64 and aarch64):
`alpine` `ubuntu` `ubuntu22` `debian` `arch` `fedora` `rocky` `opensuse` `kali` `void`.
Alpine, Ubuntu, Debian, Arch and Fedora have been verified; the others are untested.

## Profiles

```
~/.nome/NAME/
├── rootfs/   the distro (or a symlink to your own, from `nome add`)
├── home/     user data, mounted as /root — survives reinstalling the distro
├── own       fake file ownership, remembered between runs
└── binds     optional extra bind mounts, one HOST:GUEST per line
```

`nome` binds `/proc`, `/dev` and `/etc/resolv.conf` automatically. Set `NO_COLOR=1` to disable colors.

## Build

`build.c` is the only build system: `cc build.c -o build && ./build [build|install|uninstall]`.
`emuroot/` is a vendored copy of emuroot, compiled into the same binary.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). Because emuroot (GPL-3.0) is compiled in, `nome` is GPL-3.0 too.

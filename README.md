# nome

**Switch between Linux distros and their user data — powered by Docker, one small binary.**

`nome` gives every distro its own *profile*: a persistent Docker container plus a private `/root`
that lives in `~/.nome/NAME/home` on the host. Pick one in a colorful TUI, or run commands straight from your shell.

```sh
nome get ubuntu mo      # create Ubuntu 24.04 as profile "mo"
nome mo                 # open bash inside it
nome mo ls /            # run any command inside it
nome                    # guided TUI
```

**[Watch the demo](https://mkalmousli.github.io/nome/)** (real asciinema recording) · `man nome` · `nome --help`

## Install

Needs [Docker](https://docs.docker.com/get-docker/) (or podman: `NOME_DOCKER=podman`) and a C compiler.

```sh
git clone --depth 1 https://github.com/mkalmousli/nome && cd nome && ./build.c
```

Installs `nome` to `~/.local/bin` and the man page (`man nome`); as root it uses `/usr/local`.
Change with `./build.c --prefix DIR`; remove with `./build.c uninstall`; see `./build.c --help`.

Prebuilt binaries for Linux (x86_64, aarch64, armv7, i386, s390x) and macOS (x86_64, aarch64) are attached to every
[release](https://github.com/mkalmousli/nome/releases) as `nome-<os>-<arch>`.

**Updating:** `nome` checks for a new release at most once a day (interactive use only, silent offline;
`NOME_NO_UPDATE_CHECK=1` disables it) and tells you; `nome update` swaps in the new binary for your platform.

## Usage

| Command | Does |
|---|---|
| `nome` | simple TUI: arrow keys + Enter to pick a profile, then open, rename, copy, share folders or delete; "+ New profile" offers a system dropdown |
| `nome distros` | list ready-to-use distros |
| `nome get DISTRO [NAME]` | create a profile (`DISTRO` can also be any Docker image, e.g. `ubuntu:20.04`) |
| `nome add NAME ROOTFS` | import your own extracted root filesystem |
| `nome NAME [CMD...]` | run `CMD` in the profile (default: `bash`, or `sh`) |
| `nome cp OLD NEW` / `nome mv OLD NEW` | duplicate (system and data) / rename a profile |
| `nome mount NAME HOST[:GUEST]` | share a host folder (default guest path `/mnt/<folder>`) |
| `nome umount NAME PATH` / `nome mounts NAME` | stop sharing (by host or guest path) / list shares |
| `nome ls` / `nome rm NAME` | list / delete profiles |
| `nome update` | upgrade to the latest release |

Ready-to-use distros: `alpine` `ubuntu` `ubuntu22` `debian` `arch` `fedora` `rocky` `opensuse` `kali` `void`
(official multi-arch Docker images, so they match your CPU).

## Profiles

```
container nome-NAME     the system; packages you install persist
~/.nome/NAME/
├── home/               user data, mounted as /root — survives reinstalling the distro
├── binds               extra bind mounts, one HOST:GUEST per line (managed by `nome mount`)
└── image               the image the container is rebuilt from
```

- New profiles ask whether to mount the host at `/host` (default yes).
- The shell starts with a clean environment: only `TERM` is passed in, never your host variables.
- Changing mounts commits the container to an image and recreates it, so installed packages are kept.
- With a rootful Docker daemon, files created in `/root` are owned by root on the host; `nome rm` cleans them up for you.

## Contributing

Issues and PRs welcome, see [CONTRIBUTING.md](CONTRIBUTING.md). The demo is reproducible:
`HOME=$(mktemp -d) asciinema rec -c docs/demo.sh docs/demo.cast`.

## Build

`build.c` is the only build system and runs directly (no argument = build and install):
`./build.c [build|uninstall|clean]`. It picks a compiler, falls back to dynamic linking if static fails, warns if the
bin dir is not in `PATH`, and supports `--prefix` / `--destdir` for packagers.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE).

// nome - switch between Linux distros and their user data. Standalone: emuroot is built in.
//   nome                      guided TUI            nome distros | ls
//   nome get DISTRO [NAME]    download a distro     nome add NAME ROOTFS | rm NAME
//   nome NAME [CMD...]        run CMD (default: shell) inside profile NAME
// Profile layout: ~/.nome/NAME/{rootfs, home (-> /root), own, binds (optional HOST:GUEST lines)}
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include "emuroot/include/emuroot.h"

#define MAXP 64
#ifndef NOME_VERSION
#define NOME_VERSION "1.3.0"
#endif
#define REPO "mkalmousli/nome"
#if defined(__x86_64__)
#define NOME_ARCH "x86_64"
#elif defined(__aarch64__)
#define NOME_ARCH "aarch64"
#elif defined(__arm__)
#define NOME_ARCH "armv7"
#elif defined(__i386__)
#define NOME_ARCH "i386"
#else
#define NOME_ARCH "s390x"
#endif
static char base[512];
static const char *B = "", *R = "", *G = "", *Y = "", *C = "", *Z = "";  // ANSI colors

static const struct { const char *key, *path, *desc; } distros[] = {  // linuxcontainers.org images
	{"alpine", "alpine/3.22", "Alpine 3.22     tiny, musl, apk"},
	{"ubuntu", "ubuntu/noble", "Ubuntu 24.04    LTS, apt"},
	{"ubuntu22", "ubuntu/jammy", "Ubuntu 22.04    LTS, apt"},
	{"debian", "debian/trixie", "Debian 13       stable, apt"},
	{"arch", "archlinux/current", "Arch Linux      rolling, pacman"},
	{"fedora", "fedora/43", "Fedora 43       dnf"},
	{"rocky", "rockylinux/9", "Rocky Linux 9   RHEL-compatible, dnf"},
	{"opensuse", "opensuse/tumbleweed", "openSUSE        rolling, zypper"},
	{"kali", "kali/current", "Kali Linux      security tools, apt"},
	{"void", "voidlinux/current", "Void Linux      runit, xbps"},
};
#define ND ((int)(sizeof distros / sizeof *distros))

/* ---------- helpers ---------- */

static int err(const char *msg) { return fprintf(stderr, "%snome:%s %s\n", R, Z, msg), 1; }

static char *at(const char *name, const char *leaf) {  // ~/.nome/NAME/LEAF (4 rotating buffers)
	static char buf[4][1024]; static int k;
	char *b = buf[k++ % 4];
	snprintf(b, 1024, "%s/%s/%s", base, name, leaf);
	return b;
}

static int sh(const char *fmt, ...) {
	char cmd[4096]; va_list ap;
	va_start(ap, fmt); vsnprintf(cmd, sizeof cmd, fmt, ap); va_end(ap);
	return system(cmd);
}

static int valid(const char *n) {  // profile names: [A-Za-z0-9_-], never a subcommand
	static const char *cmds[] = {"add", "rm", "ls", "get", "distros", "help", "version", "cp", "mv", "mount", "umount", "mounts", "update"};
	for (int i = 0; i < (int)(sizeof cmds / sizeof *cmds); i++) if (!strcmp(n, cmds[i])) return 0;
	if (!*n) return 0;
	for (; *n; n++) if (!isalnum(*n) && *n != '-' && *n != '_') return 0;
	return 1;
}

static int list(char names[][64]) {
	int n = 0; struct dirent *e; DIR *d = opendir(base);
	while (d && (e = readdir(d)) && n < MAXP)
		if (e->d_name[0] != '.' && strlen(e->d_name) < 64) strcpy(names[n++], e->d_name);
	if (d) closedir(d);
	return n;
}

/* ---------- profile management ---------- */

static void show_distros(void) {
	for (int i = 0; i < ND; i++) printf("  %s%2d)%s %s%-9s%s %s\n", C, i + 1, Z, B, distros[i].key, Z, distros[i].desc);
}

static int rm(const char *name) {
	if (!valid(name)) return err("bad profile name");
	sh("chmod -R u+rwX '%s' 2>/dev/null; rm -rf '%s'", at(name, ""), at(name, ""));  // symlinked rootfs stays
	return printf("%sremoved%s %s\n", Y, Z, name), 0;
}

static int cp(const char *src, const char *dst, int move) {  // duplicate (or rename) a profile
	if (!valid(src) || !valid(dst)) return err("bad profile name");
	if (access(at(src, ""), F_OK)) return err("no such profile (try: nome ls)");
	if (!access(at(dst, ""), F_OK)) return err("destination already exists");
	char tmp[8];
	if (move ? rename(at(src, ""), at(dst, "")) :  // emuroot may leave 0000 files: make them readable first
	    sh("chmod -R u+rwX '%s' 2>/dev/null; cp -a '%s' '%s'", at(src, ""), at(src, ""), at(dst, "")))
		return rm(dst), err("copy failed");
	printf("%s%s%s %s -> %s\n", G, move ? "renamed" : "copied", Z, src, dst);
	if (readlink(at(dst, "rootfs"), tmp, sizeof tmp) > 0) printf("%snote: linked rootfs is shared, home is separate%s\n", Y, Z);
	return 0;
}

static int add(const char *name, const char *rootfs) {  // register an existing rootfs directory
	char *real = realpath(rootfs, NULL);
	if (!valid(name) || !real) return err("bad name or rootfs path");
	mkdir(base, 0755); mkdir(at(name, ""), 0755); mkdir(at(name, "home"), 0755);
	if (symlink(real, at(name, "rootfs"))) return perror("nome"), 1;
	return printf("%sadded%s %s\n", G, Z, name), 0;
}

static int mounts(const char *name, const char *drop, const char *add) {  // list / add / remove folder mounts
	char line[1100], keep[64][1100], *host = NULL; int n = 0;
	FILE *f = fopen(at(name, "binds"), "r");
	if (access(at(name, ""), F_OK)) return err("no such profile (try: nome ls)");
	if (add) {  // "HOST[:GUEST]" -> "HOST:GUEST", GUEST defaults to /mnt/<folder>
		char *sep = strchr(add, ':'), *real = realpath(sep ? strndup(add, sep - add) : add, NULL);
		if (!real) return err("host folder not found");
		snprintf(keep[n++], 1100, "%s:%s", real, sep ? sep + 1 : "");
		if (!sep) snprintf(keep[0], 1100, "%s:/mnt/%s", real, strrchr(real, '/') + 1);
	}
	while (f && fgets(line, sizeof line, f) && n < 63) {
		line[strcspn(line, "\n")] = 0; host = strchr(line, ':');
		if (!*line || !host) continue;
		if (drop && (!strcmp(host + 1, drop) || (*host = 0, !strcmp(line, drop)))) continue;
		if (*host == 0) *host = ':';
		if (add && !strcmp(line, keep[0])) continue;
		strcpy(keep[n++], line);
	}
	if (f) fclose(f);
	if (!add && !drop) { for (int i = 0; i < n; i++) printf("  %s%s%s\n", C, keep[i], Z); return 0; }
	if (!(f = fopen(at(name, "binds"), "w"))) return err("cannot write binds file");
	for (int i = 0; i < n; i++) fprintf(f, "%s\n", keep[i]);
	return fclose(f), printf("%s%s%s\n", G, add ? "mounted" : "unmounted", Z), 0;
}

static int get(const char *key, const char *name) {  // download + unpack a ready-to-use distro
	struct utsname u; char url[256]; int i;
	for (i = 0; i < ND && strcmp(distros[i].key, key); i++);
	uname(&u);
	if (i == ND) return err("unknown distro (see: nome distros)");
	if (!valid(name)) return err("bad profile name");
	if (!access(at(name, ""), F_OK)) return err("profile already exists");
	snprintf(url, sizeof url, "https://images.linuxcontainers.org/images/%s/%s/default/", distros[i].path,
		 !strcmp(u.machine, "aarch64") ? "arm64" : "amd64");
	mkdir(base, 0755); mkdir(at(name, ""), 0755); mkdir(at(name, "rootfs"), 0755); mkdir(at(name, "home"), 0755);
	printf("%sinstalling%s %s as '%s'...\n", C, Z, distros[i].desc, name);
	if (sh("d=$(curl -fsSL %s | grep -oE '[0-9]{8}_[0-9:]+/' | tail -1) && curl -fL%s %s$d/rootfs.tar.xz | "
	       "tar -xJ -C '%s' --no-same-owner --exclude='dev/*' --exclude='./dev/*' 2>/dev/null",
	       url, isatty(2) ? "#" : "sS", url, at(name, "rootfs")))
		return rm(name), err("download failed (needs curl, tar, xz)");
	return printf("%sready:%s nome %s\n", G, Z, name), 0;
}

/* ---------- running ---------- */

static void bind(emuroot_ctx_t *x, char *spec) {  // "HOST:GUEST"
	char *sep = strchr(spec, ':');
	if (sep) *sep = 0, emuroot_bind(x, spec, sep + 1);
}

static int run(const char *name, char **cmd, int n) {  // emuroot is linked in: no external dependency
	char line[512], *shell[] = {"/bin/sh", NULL}; FILE *f; int rc;
	emuroot_ctx_t *x = emuroot_create();
	if (access(at(name, "rootfs"), F_OK)) return err("no such profile (try: nome ls)");
	emuroot_set_root(x, at(name, "rootfs"));
	emuroot_set_fake_ids(x, 0, 0);
	emuroot_set_ids_file(x, at(name, "own"));
	emuroot_bind(x, "/proc", "/proc"); emuroot_bind(x, "/dev", "/dev");
	emuroot_bind(x, "/etc/resolv.conf", "/etc/resolv.conf");
	emuroot_bind(x, at(name, "home"), "/root");
	for (f = fopen(at(name, "binds"), "r"); f && fgets(line, sizeof line, f);)  // extra user bindings
		line[strcspn(line, "\n")] = 0, bind(x, line);
	if (!access(at(name, "rootfs/bin/bash"), X_OK)) shell[0] = "/bin/bash";
	setenv("HOME", "/root", 1);
	setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
	unsetenv("LD_LIBRARY_PATH");
	if ((rc = emuroot_run(x, n ? cmd : shell, NULL)) < 0) return err(emuroot_strerror(rc)), 127;
	return emuroot_destroy(x), rc;
}

/* ---------- TUI ---------- */

static char *ask(const char *q, char *buf) {
	printf("%s%s%s", B, q, Z); fflush(stdout);
	if (!fgets(buf, 512, stdin)) exit(0);
	buf[strcspn(buf, "\n")] = 0;
	return buf;
}

static int tui(void) {
	char names[MAXP][64], in[512], in2[512];
	for (;;) {
		int n = list(names), k;
		printf("\n%s  nome%s %s- Linux distros & user data%s\n\n", G, Z, Y, Z);
		for (int i = 0; i < n; i++) printf("  %s%2d)%s %s\n", C, i + 1, Z, names[i]);
		if (!n) printf("  %sno profiles yet - press i to install a distro%s\n", Y, Z);
		ask("\n  [number] enter  [i]nstall  [a]dd  [c]opy  [m]ount  [d]elete  [q]uit > ", in);
		if ((k = atoi(in)) > 0 && k <= n) return run(names[k - 1], NULL, 0);
		if (*in == 'q') return 0;
		if (*in == 'i') {
			printf("\n"); show_distros();
			if ((k = atoi(ask("\n  pick a distro > ", in))) > 0 && k <= ND) {
				ask("  profile name (enter = default) > ", in2);
				get(distros[k - 1].key, *in2 ? in2 : distros[k - 1].key);
			}
		} else if (*in == 'a') {
			ask("  profile name > ", in);
			add(in, ask("  path to extracted rootfs > ", in2));
		} else if (*in == 'c' && (k = atoi(ask("  number to copy > ", in))) > 0 && k <= n) {
			cp(names[k - 1], ask("  new name > ", in2), 0);
		} else if (*in == 'm' && (k = atoi(ask("  number to share a folder with > ", in))) > 0 && k <= n) {
			mounts(names[k - 1], NULL, ask("  folder (HOST or HOST:GUEST) > ", in2));
		} else if (*in == 'd' && (k = atoi(ask("  number to delete > ", in))) > 0 && k <= n)
			rm(names[k - 1]);
	}
}

/* ---------- updates ---------- */

static int newer(const char *a, const char *b) {  // is version a newer than b?
	int x[3] = {0}, y[3] = {0};
	sscanf(a, "%d.%d.%d", x, x + 1, x + 2), sscanf(b, "%d.%d.%d", y, y + 1, y + 2);
	for (int i = 0; i < 3; i++) if (x[i] != y[i]) return x[i] > y[i];
	return 0;
}

static int latest(char *out, size_t len) {  // newest released version; 0 if GitHub is unreachable
	FILE *p = popen("curl -fsSL --max-time 3 -o /dev/null -w '%{url_effective}' https://github.com/" REPO "/releases/latest"
			" 2>/dev/null | grep -oE '/tag/v[0-9.]+$' | grep -oE '[0-9.]+$'", "r");  // redirect target: no API rate limit
	*out = 0;
	if (p) { if (fgets(out, len, p)) out[strcspn(out, "\n")] = 0; pclose(p); }
	return *out != 0;
}

static void check_update(void) {  // at most once a day, interactive use only; cached in ~/.nome/.latest
	char path[600], v[32] = ""; struct stat st; FILE *f;
	if (getenv("NOME_NO_UPDATE_CHECK") || !isatty(1) || !isatty(2)) return;
	snprintf(path, sizeof path, "%s/.latest", base);
	if (stat(path, &st) || time(NULL) - st.st_mtime > 86400) {
		mkdir(base, 0755);
		if (!latest(v, sizeof v)) strcpy(v, NOME_VERSION);
		if ((f = fopen(path, "w"))) fputs(v, f), fclose(f);
	} else if ((f = fopen(path, "r"))) v[fread(v, 1, sizeof v - 1, f)] = 0, v[strcspn(v, "\n")] = 0, fclose(f);
	if (newer(v, NOME_VERSION))
		fprintf(stderr, "%snome %s is available (you have %s) - run: nome update%s\n", Y, v, NOME_VERSION, Z);
}

static int update(void) {  // replace this binary with the latest release build for this arch
	char v[32], exe[1024], tmp[1030], path[600]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0) return err("cannot locate own binary");
	exe[n] = 0;
	if (!latest(v, sizeof v)) return err("cannot reach GitHub (no network?)");
	if (!newer(v, NOME_VERSION)) return printf("nome %s is up to date\n", NOME_VERSION), 0;
	printf("updating %s -> %s\n", NOME_VERSION, v), snprintf(tmp, sizeof tmp, "%s.new", exe);
	if (sh("curl -fL# -o '%s' https://github.com/" REPO "/releases/download/v%s/nome-linux-" NOME_ARCH
	       " && chmod 755 '%s' && '%s' --version >/dev/null && mv -f '%s' '%s'", tmp, v, tmp, tmp, tmp, exe))
		return unlink(tmp), err("update failed (no write access to the binary's folder? or rebuild: git pull && ./build.c)");
	snprintf(path, sizeof path, "%s/.latest", base), unlink(path);
	return printf("%supdated to %s%s\n", G, v, Z), 0;
}

static int usage(void) {
	printf("%snome%s %s- Linux distros & user data, no root needed%s\n\n"
	       "  %snome%s                      guided TUI\n"
	       "  %snome%s NAME [CMD...]        run CMD (default: shell) in profile NAME\n"
	       "  %snome get%s DISTRO [NAME]    download a ready-to-use distro (see %snome distros%s)\n"
	       "  %snome add%s NAME ROOTFS      register your own root filesystem\n"
	       "  %snome cp%s|%smv%s OLD NEW        duplicate / rename a profile\n"
	       "  %snome mount%s NAME HOST[:GUEST]   share a folder (default guest: /mnt/<name>)\n"
	       "  %snome umount%s NAME PATH     unshare;  %snome mounts%s NAME  lists them\n"
	       "  %snome ls%s | %srm%s NAME         list / delete profiles\n"
	       "  %snome update%s               upgrade nome (checked automatically once a day)\n\n"
	       "Profiles live in ~/.nome. More: man nome\n", G, Z, Y, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z, C, Z);
	return 0;
}

static int dispatch(int argc, char **argv) {
	char nm[MAXP][64]; int n;
	snprintf(base, sizeof base, "%s/.nome", getenv("HOME") ?: ".");
	if (isatty(1) && !getenv("NO_COLOR")) B = "\033[1m", R = "\033[31m", G = "\033[32m", Y = "\033[33m", C = "\033[36m", Z = "\033[0m";
	if (argc < 2) return tui();
	if (!strcmp(argv[1], "help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) return usage();
	if (!strcmp(argv[1], "version") || !strcmp(argv[1], "--version")) return puts("nome " NOME_VERSION), 0;
	if (!strcmp(argv[1], "update")) return update();
	if (!strcmp(argv[1], "ls")) { for (n = list(nm); n--;) puts(nm[n]); return 0; }
	if (!strcmp(argv[1], "distros")) return show_distros(), 0;
	if (!strcmp(argv[1], "add") && argc == 4) return add(argv[2], argv[3]);
	if ((!strcmp(argv[1], "cp") || !strcmp(argv[1], "mv")) && argc == 4) return cp(argv[2], argv[3], argv[1][0] == 'm');
	if (!strcmp(argv[1], "mount") && argc == 4) return mounts(argv[2], NULL, argv[3]);
	if (!strcmp(argv[1], "umount") && argc == 4) return mounts(argv[2], argv[3], NULL);
	if (!strcmp(argv[1], "mounts") && argc == 3) return mounts(argv[2], NULL, NULL);
	if (!strcmp(argv[1], "rm") && argc == 3) return rm(argv[2]);
	if (!strcmp(argv[1], "get") && argc >= 3) return get(argv[2], argc > 3 ? argv[3] : argv[2]);
	return run(argv[1], argv + 2, argc - 2);
}

int main(int argc, char **argv) {
	int rc = dispatch(argc, argv);
	if (argc < 2 || strcmp(argv[1], "update")) check_update();
	return rc;
}

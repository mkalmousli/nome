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
#include <unistd.h>
#include "emuroot/include/emuroot.h"

#define MAXP 64
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

static int valid(const char *n) {
	if (!*n || !strcmp(n, "add") || !strcmp(n, "rm") || !strcmp(n, "ls") || !strcmp(n, "get") || !strcmp(n, "distros")) return 0;
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

static int add(const char *name, const char *rootfs) {  // register an existing rootfs directory
	char *real = realpath(rootfs, NULL);
	if (!valid(name) || !real) return err("bad name or rootfs path");
	mkdir(base, 0755); mkdir(at(name, ""), 0755); mkdir(at(name, "home"), 0755);
	if (symlink(real, at(name, "rootfs"))) return perror("nome"), 1;
	return printf("%sadded%s %s\n", G, Z, name), 0;
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
		ask("\n  [number] enter  [i]nstall  [a]dd custom  [d]elete  [q]uit > ", in);
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
		} else if (*in == 'd' && (k = atoi(ask("  number to delete > ", in))) > 0 && k <= n)
			rm(names[k - 1]);
	}
}

int main(int argc, char **argv) {
	char nm[MAXP][64]; int n;
	snprintf(base, sizeof base, "%s/.nome", getenv("HOME") ?: ".");
	if (isatty(1) && !getenv("NO_COLOR")) B = "\033[1m", R = "\033[31m", G = "\033[32m", Y = "\033[33m", C = "\033[36m", Z = "\033[0m";
	if (argc < 2) return tui();
	if (!strcmp(argv[1], "ls")) { for (n = list(nm); n--;) puts(nm[n]); return 0; }
	if (!strcmp(argv[1], "distros")) return show_distros(), 0;
	if (!strcmp(argv[1], "add") && argc == 4) return add(argv[2], argv[3]);
	if (!strcmp(argv[1], "rm") && argc == 3) return rm(argv[2]);
	if (!strcmp(argv[1], "get") && argc >= 3) return get(argv[2], argc > 3 ? argv[3] : argv[2]);
	return run(argv[1], argv + 2, argc - 2);
}

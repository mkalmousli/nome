// nome - switch between Linux distros and their user data, powered by Docker.
//   nome                      guided TUI            nome distros | ls
//   nome get DISTRO [NAME]    create a profile      nome add NAME ROOTFS | rm NAME
//   nome NAME [CMD...]        run CMD (default: shell) inside profile NAME
// A profile is a persistent container "nome-NAME" plus ~/.nome/NAME/{home (-> /root), binds (HOST:GUEST lines), image}
#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#define NOME_OS "macos"
#else
#define NOME_OS "linux"
#endif

#define MAXP 64
#ifndef NOME_VERSION
#define NOME_VERSION "2.1.0"
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
static const char *dk = "docker";  // container engine; NOME_DOCKER=podman works too
static const char *B = "", *R = "", *G = "", *Y = "", *C = "", *Z = "";  // ANSI colors

static const struct { const char *key, *image, *desc; } distros[] = {  // Docker Hub images (multi-arch)
	{"alpine", "alpine:3.22", "Alpine 3.22     tiny, musl, apk"},
	{"ubuntu", "ubuntu:24.04", "Ubuntu 24.04    LTS, apt"},
	{"ubuntu22", "ubuntu:22.04", "Ubuntu 22.04    LTS, apt"},
	{"debian", "debian:trixie", "Debian 13       stable, apt"},
	{"arch", "archlinux:latest", "Arch Linux      rolling, pacman"},
	{"fedora", "fedora:43", "Fedora 43       dnf"},
	{"rocky", "rockylinux:9", "Rocky Linux 9   RHEL-compatible, dnf"},
	{"opensuse", "opensuse/tumbleweed", "openSUSE        rolling, zypper"},
	{"kali", "kalilinux/kali-rolling", "Kali Linux      security tools, apt"},
	{"void", "ghcr.io/void-linux/void-glibc", "Void Linux      runit, xbps"},
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

static int has(const char *name) { return !access(at(name, ""), F_OK); }
static int engine(void) {
	if (!sh("%s version >/dev/null 2>&1", dk)) return 0;
	return err("Docker is not installed or not running (set NOME_DOCKER=podman to use podman)");
}
static int live(const char *name) { return !sh("%s inspect nome-%s >/dev/null 2>&1", dk, name); }

static char *image_of(const char *name) {  // image the container is (re)created from
	static char img[256]; FILE *f = fopen(at(name, "image"), "r");
	*img = 0;
	if (f) { if (fgets(img, sizeof img, f)) img[strcspn(img, "\n")] = 0; fclose(f); }
	return img;
}

static void set_image(const char *name, const char *img) {
	FILE *f = fopen(at(name, "image"), "w");
	if (f) fprintf(f, "%s\n", img), fclose(f);
}

static char *tag(const char *name) {  // docker image tags must be lowercase
	static char t[128];
	snprintf(t, sizeof t, "nome-image/%s", name);
	for (char *c = t; *c; c++) *c = tolower((unsigned char)*c);
	return t;
}

static int make(const char *name, const char *image) {  // (re)create the persistent container: home + user mounts
	char line[1100], opts[8192] = ""; FILE *f = fopen(at(name, "binds"), "r");
	while (f && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\n")] = 0;
		if (*line) snprintf(opts + strlen(opts), sizeof opts - strlen(opts), " -v '%s'", line);
	}
	if (f) fclose(f);
	return sh("%s create -it --init --name nome-%s --hostname %s -w /root -v '%s:/root'%s '%s' sleep infinity >/dev/null",
		  dk, name, name, at(name, "home"), opts, image);
}

static int recreate(const char *name, const char *from) {  // keep installed packages: commit, then rebuild with new mounts
	const char *img = tag(name);
	if (sh("%s commit nome-%s %s >/dev/null && %s rm -f nome-%s >/dev/null", dk, from, img, dk, from)) return 1;
	set_image(name, img);
	return make(name, img);
}

static int rm(const char *name) {
	if (!valid(name)) return err("bad profile name");
	if (!engine() && live(name))  // root-owned files in home are removed from inside, then the container goes
		sh("%s start nome-%s >/dev/null 2>&1; %s exec nome-%s sh -c 'rm -rf /root/* /root/.[!.]* /root/..?*' 2>/dev/null; %s rm -f nome-%s >/dev/null 2>&1; %s rmi %s >/dev/null 2>&1",
		   dk, name, dk, name, dk, name, dk, tag(name));
	sh("rm -rf '%s'", at(name, ""));
	return printf("%sremoved%s %s\n", Y, Z, name), 0;
}

static int cp(const char *src, const char *dst, int move) {  // duplicate (or rename) a profile
	if (!valid(src) || !valid(dst)) return err("bad profile name");
	if (!has(src)) return err("no such profile (try: nome ls)");
	if (has(dst)) return err("destination already exists");
	if (engine()) return 1;
	if (move ? rename(at(src, ""), at(dst, "")) : sh("cp -a '%s' '%s'", at(src, ""), at(dst, "")))
		return err("copy failed (is the home folder readable?)");
	if (move) {  // container mounts the old home path: commit it and rebuild under the new name
		if (sh("%s commit nome-%s %s >/dev/null && %s rm -f nome-%s >/dev/null", dk, src, tag(dst), dk, src)) return rm(dst), err("rename failed");
	} else if (sh("%s commit nome-%s %s >/dev/null", dk, src, tag(dst))) return rm(dst), err("copy failed");
	set_image(dst, tag(dst));
	if (make(dst, tag(dst))) return rm(dst), err("could not create container");
	if (move) sh("%s rmi %s >/dev/null 2>&1", dk, tag(src));
	return printf("%s%s%s %s -> %s\n", G, move ? "renamed" : "copied", Z, src, dst), 0;
}

static void offer_host(const char *name);

static int add(const char *name, const char *rootfs) {  // import an existing rootfs directory as a new profile
	char *real = realpath(rootfs, NULL);
	if (!valid(name) || !real) return err("bad name or rootfs path");
	if (has(name)) return err("profile already exists");
	if (engine()) return 1;
	mkdir(base, 0755); mkdir(at(name, ""), 0755); mkdir(at(name, "home"), 0755);
	if (sh("tar -C '%s' -c . | %s import - %s >/dev/null", real, dk, tag(name))) return rm(name), err("import failed");
	set_image(name, tag(name));
	printf("%sadded%s %s\n", G, Z, name), offer_host(name);
	if (make(name, tag(name))) return rm(name), err("could not create container");
	return 0;
}

static int mounts(const char *name, const char *drop, const char *add) {  // list / add / remove folder mounts
	char line[1100], keep[64][1100], *host = NULL; int n = 0;
	FILE *f = fopen(at(name, "binds"), "r");
	if (!has(name)) return err("no such profile (try: nome ls)");
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
	fclose(f);
	if (engine() || (live(name) && recreate(name, name))) return err("could not apply the change to the container");
	return printf("%s%s%s\n", G, add ? "mounted" : "unmounted", Z), 0;
}

static void offer_host(const char *name) {  // new profiles: ask whether to mount the host's / at /host (default yes)
	char a[16] = "";
	if (isatty(0)) {
		printf("Mount the host system at /host inside it? [Y/n] "), fflush(stdout);
		if (!fgets(a, sizeof a, stdin)) *a = 0;
	}
	if (*a != 'n' && *a != 'N') mounts(name, NULL, "/:/host");
}

static int get(const char *key, const char *name) {  // create a profile from a distro (or any Docker image)
	const char *image = NULL; int i;
	for (i = 0; i < ND; i++) if (!strcmp(distros[i].key, key)) image = distros[i].image;
	if (!image && (strchr(key, ':') || strchr(key, '/'))) image = key;
	if (!image) return err("unknown distro (see: nome distros, or pass a Docker image like ubuntu:20.04)");
	if (!valid(name)) return err("bad profile name (use letters, digits, - and _; for an image pass NAME)");
	if (has(name)) return err("profile already exists");
	if (engine()) return 1;
	mkdir(base, 0755); mkdir(at(name, ""), 0755); mkdir(at(name, "home"), 0755);
	set_image(name, image);
	printf("%sinstalling%s %s as '%s'...\n", C, Z, image, name);
	if (sh("%s pull %s '%s' >/dev/null", dk, isatty(2) ? "" : "-q", image) && sh("%s pull '%s'", dk, image))
		return rm(name), err("pull failed (network? image name?)");
	printf("%sready:%s nome %s\n", G, Z, name), offer_host(name);
	if (make(name, image)) return rm(name), err("could not create container");
	return 0;
}

/* ---------- running ---------- */

static int run(const char *name, char **cmd, int n) {
	char *shell[] = {"sh", "-c", "command -v bash >/dev/null 2>&1 && exec bash || exec sh", NULL};
	char *argv[256], term[128], cname[80]; int k = 0, st; pid_t p;
	if (!has(name)) return err("no such profile (try: nome ls)");
	if (n > 240) return err("too many arguments");
	if (engine()) return 127;
	if (!live(name) && make(name, image_of(name))) return err("could not create container");
	if (sh("%s start nome-%s >/dev/null", dk, name)) return err("could not start container");
	snprintf(term, sizeof term, "TERM=%s", getenv("TERM") ?: "xterm");  // the guest sees only this, never the host environment
	snprintf(cname, sizeof cname, "nome-%s", name);
	argv[k++] = (char *)dk, argv[k++] = "exec", argv[k++] = isatty(0) ? "-it" : "-i";
	argv[k++] = "-e", argv[k++] = term, argv[k++] = "-w", argv[k++] = "/root", argv[k++] = cname;
	if (n) for (int i = 0; i < n; i++) argv[k++] = cmd[i];
	else for (char **c = shell; *c; c++) argv[k++] = *c;
	argv[k] = NULL;
	fflush(stdout);
	if (!(p = fork())) execvp(dk, argv), _exit(127);
	if (p < 0 || waitpid(p, &st, 0) < 0) return err("cannot run docker");
	return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
}

static int usage(void);

/* ---------- TUI ---------- */

static char *ask(const char *q, char *buf) {
	printf("%s%s%s", B, q, Z); fflush(stdout);
	if (!fgets(buf, 512, stdin)) exit(0);
	buf[strcspn(buf, "\n")] = 0;
	return buf;
}

static int pick(const char *title, const char **items, int n, int sel) {  // arrow-key dropdown; -1 = back
	struct termios o, r; int i, c, rc = -1; char k, k2;
	tcgetattr(0, &o), r = o, r.c_lflag &= ~(ICANON | ECHO | ISIG), r.c_cc[VMIN] = 0, r.c_cc[VTIME] = 1;
	tcsetattr(0, TCSANOW, &r);
	printf("\033[?25l\n  %s%s%s\n\n", B, title, Z);
	for (;;) {
		for (i = 0; i < n; i++) printf("  %s%s %s %s\033[K\n", i == sel ? "\033[7m" : "", i == sel ? ">" : " ", items[i], Z);
		printf("  %s up/down: move   enter: select   q: back%s\033[K", Y, Z), fflush(stdout);
		while (read(0, &k, 1) < 1);
		c = k;
		if (k == 27) {  // arrow keys arrive as ESC [ A/B; a lone ESC means back
			if (read(0, &k2, 1) < 1) c = 'q';
			else if (read(0, &k2, 1) == 1) c = k2 == 'A' ? 'k' : k2 == 'B' ? 'j' : 0;
		}
		if (c == 'k' && sel > 0) sel--;
		else if (c == 'j' && sel < n - 1) sel++;
		else if (c == '\n' || c == '\r') { rc = sel; break; }
		else if (c == 'q' || c == 3 || c == 4) break;
		printf("\033[%dA\r", n);
	}
	printf("\033[%dA\r\033[J\033[?25h", n + 3), fflush(stdout);  // erase the menu, restore cursor
	tcsetattr(0, TCSANOW, &o);
	return rc;
}

static void new_profile(void) {
	const char *items[ND + 3]; char label[ND][96], in[512], in2[512]; int i, k;
	for (i = 0; i < ND; i++) snprintf(label[i], sizeof label[i], "%-9s %s", distros[i].key, distros[i].desc), items[i] = label[i];
	items[ND] = "Other Docker image...", items[ND + 1] = "Import a root filesystem folder...", items[ND + 2] = "Back";
	if ((k = pick("New profile: choose a system", items, ND + 3, 0)) < 0 || k == ND + 2) return;
	if (k < ND) {
		printf("%s%s%s\n", C, distros[k].desc, Z), ask("Profile name (enter = default) > ", in);
		get(distros[k].key, *in ? in : distros[k].key);
	} else if (k == ND) {
		ask("Docker image (e.g. ubuntu:20.04) > ", in), ask("Profile name > ", in2), get(in, in2);
	} else ask("Folder with the extracted rootfs > ", in), ask("Profile name > ", in2), add(in2, in);
}

static int profile_menu(const char *name) {  // -1: stay in the TUI, otherwise the shell's exit status
	const char *items[] = {"Open shell", "Rename", "Copy", "Share a folder", "Unshare a folder", "Delete", "Back"};
	char in[512], title[128], binds[16][512]; const char *bi[17]; int k, n = 0;
	snprintf(title, sizeof title, "%s", name);
	switch (k = pick(title, items, 7, 0)) {
	case 0: return run(name, NULL, 0);
	case 1: cp(name, ask("New name > ", in), 1); break;
	case 2: cp(name, ask("Name of the copy > ", in), 0); break;
	case 3: mounts(name, NULL, ask("Folder to share (HOST or HOST:GUEST) > ", in)); break;
	case 4: {
		FILE *f = fopen(at(name, "binds"), "r");
		while (f && n < 16 && fgets(binds[n], sizeof binds[n], f)) binds[n][strcspn(binds[n], "\n")] = 0, bi[n] = binds[n], n++;
		if (f) fclose(f);
		bi[n++] = "Back";
		if ((k = pick("Stop sharing which folder?", bi, n, 0)) >= 0 && k < n - 1) mounts(name, strchr(binds[k], ':') + 1, NULL);
		break;
	}
	case 5: {
		const char *yn[] = {"Yes, delete it and its data", "Cancel"};
		if (pick("Delete this profile?", yn, 2, 1) == 0) rm(name);
	}
	}
	return -1;
}

static int tui(void) {
	char names[MAXP][64]; const char *items[MAXP + 2]; int n, k, rc, sel = 0;
	if (!isatty(0) || !isatty(1)) return usage();
	for (;;) {
		n = list(names);
		for (int i = 0; i < n; i++) items[i] = names[i];
		items[n] = "+ New profile", items[n + 1] = "Quit";
		k = pick("nome - choose a profile", items, n + 2, sel);
		if (k < 0 || k == n + 1) return 0;
		sel = k;
		if (k == n) new_profile();
		else if ((rc = profile_menu(names[k])) >= 0) return rc;
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
	char v[32], exe[1024], tmp[1030], path[600];
#ifdef __APPLE__
	uint32_t len = sizeof exe;
	if (_NSGetExecutablePath(exe, &len)) return err("cannot locate own binary");
#else
	ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
	if (n <= 0) return err("cannot locate own binary");
	exe[n] = 0;
#endif
	if (!latest(v, sizeof v)) return err("cannot reach GitHub (no network?)");
	if (!newer(v, NOME_VERSION)) return printf("nome %s is up to date\n", NOME_VERSION), 0;
	printf("updating %s -> %s\n", NOME_VERSION, v), snprintf(tmp, sizeof tmp, "%s.new", exe);
	if (sh("curl -fL# -o '%s' https://github.com/" REPO "/releases/download/v%s/nome-" NOME_OS "-" NOME_ARCH
	       " && chmod 755 '%s' && '%s' --version >/dev/null && mv -f '%s' '%s'", tmp, v, tmp, tmp, tmp, exe))
		return unlink(tmp), err("update failed (no write access to the binary's folder? or rebuild: git pull && ./build.c)");
	snprintf(path, sizeof path, "%s/.latest", base), unlink(path);
	return printf("%supdated to %s%s\n", G, v, Z), 0;
}

static int usage(void) {
	printf("%snome%s %s- Linux distros & user data, powered by Docker%s\n\n"
	       "  %snome%s                      guided TUI\n"
	       "  %snome%s NAME [CMD...]        run CMD (default: shell) in profile NAME\n"
	       "  %snome get%s DISTRO [NAME]    create a profile from a distro (see %snome distros%s) or any image\n"
	       "  %snome add%s NAME ROOTFS      import your own root filesystem\n"
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

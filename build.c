//usr/bin/env sh -c 'd=$(dirname "$0"); o=$(mktemp) && cc -o "$o" "$0" && (cd "$d" && "$o" "$@"); r=$?; rm -f "$o"; exit $r' "$0" "$@"; exit
// build.c - the only build system. Run it directly:  ./build.c [--help]
// (the first line makes it self-compiling; or: cc build.c -o build && ./build)
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *CFLAGS = "-std=gnu11 -O2 -Wall -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -Iemuroot/include -Iemuroot/src";
static char prefix[512], dest[512], bin[1100], man[1100];

static int sh(const char *fmt, ...) {
	char cmd[4096]; va_list ap;
	va_start(ap, fmt); vsnprintf(cmd, sizeof cmd, fmt, ap); va_end(ap);
	return system(cmd);
}

static int help(int rc) {
	puts("usage: ./build.c [build|uninstall|clean] [--prefix DIR] [--destdir DIR]\n\n"
	     "  (none)      build and install nome and its man page\n"
	     "  build       only compile ./nome (static, embedded emuroot)\n"
	     "  uninstall   remove them again\n"
	     "  clean       delete build output\n\n"
	     "Update an installed nome with:  nome update\n\n"
	     "  --prefix    install root (default: /usr/local as root, else ~/.local; or $PREFIX)\n"
	     "  --destdir   staging root for packagers (or $DESTDIR)\n"
	     "  $CC         compiler to use (default: first of cc, gcc, clang)");
	return rc;
}

static int build(void) {
	const char *cc = getenv("CC"), *try[] = {"cc", "gcc", "clang"};
	char pick[64] = "";
	if (cc) snprintf(pick, sizeof pick, "%s", cc);
	for (int i = 0; !*pick && i < 3; i++) if (!sh("command -v %s >/dev/null 2>&1", try[i])) snprintf(pick, sizeof pick, "%s", try[i]);
	if (!*pick) return fprintf(stderr, "build: no C compiler found (install gcc or clang)\n"), 1;
	printf("building with %s...\n", pick);
	const char *src = "-o nome nome.c $(ls emuroot/src/*.c)";
	if (!sh("%s %s -static %s 2>/dev/null", pick, CFLAGS, src)) return puts("built ./nome (static)"), 0;
	puts("static linking unavailable, building dynamically");
	return sh("%s %s %s", pick, CFLAGS, src) ? 1 : puts("built ./nome (dynamic)"), 0;
}

int main(int argc, char **argv) {
	const char *act = "install", *p = getenv("PREFIX"), *dd = getenv("DESTDIR") ?: "", *home = getenv("HOME") ?: ".";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help") || !strcmp(argv[i], "help")) return help(0);
		else if (!strcmp(argv[i], "--prefix") && i + 1 < argc) p = argv[++i];
		else if (!strcmp(argv[i], "--destdir") && i + 1 < argc) dd = argv[++i];
		else if (argv[i][0] != '-') act = argv[i];
		else return help(1);
	}
	if (p) snprintf(prefix, sizeof prefix, "%s", p);
	else if (geteuid() == 0) snprintf(prefix, sizeof prefix, "/usr/local");
	else snprintf(prefix, sizeof prefix, "%s/.local", home);
	snprintf(bin, sizeof bin, "%s%s/bin/nome", dd, prefix);
	snprintf(man, sizeof man, "%s%s/share/man/man1/nome.1", dd, prefix);

	if (!strcmp(act, "build")) return build();
	if (!strcmp(act, "clean")) return sh("rm -f nome build");
	if (!strcmp(act, "install")) {
		if (build()) return 1;
		if (sh("install -D -m755 nome '%s' && install -D -m644 man/nome.1 '%s'", bin, man)) return fprintf(stderr, "install failed (try --prefix ~/.local or sudo)\n"), 1;
		printf("installed: %s\ninstalled: %s\n", bin, man);
		if (!*dd && sh("case \":$PATH:\" in *\":%s/bin:\"*) ;; *) exit 1;; esac", prefix))
			printf("note: %s/bin is not in your PATH\n", prefix);
		return 0;
	}
	if (!strcmp(act, "uninstall")) return sh("rm -f '%s' '%s' && echo 'removed nome and its man page'", bin, man);
	return help(1);
}

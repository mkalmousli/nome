// Bootstrap: cc build.c -o build && ./build [build|install|uninstall]
// Builds one fully static binary (nome + embedded emuroot). PREFIX defaults to ~/.local.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
	const char *act = argc > 1 ? argv[1] : "build", *home = getenv("HOME") ?: ".", *prefix = getenv("PREFIX");
	char cmd[2048], dest[512];
	if (prefix) snprintf(dest, sizeof dest, "%s/bin/nome", prefix);
	else snprintf(dest, sizeof dest, "%s/.local/bin/nome", home);
	const char *cc = "${CC:-cc} -std=gnu11 -O2 -Wall -static -D_GNU_SOURCE -D_LARGEFILE64_SOURCE -Iemuroot/include -Iemuroot/src "
			 "-o nome nome.c $(ls emuroot/src/*.c)";
	if (!strcmp(act, "build")) snprintf(cmd, sizeof cmd, "%s", cc);
	else if (!strcmp(act, "install"))
		snprintf(cmd, sizeof cmd, "%s && mkdir -p \"$(dirname '%s')\" && install -m755 nome '%s' && echo installed: '%s'", cc, dest, dest, dest);
	else if (!strcmp(act, "uninstall")) snprintf(cmd, sizeof cmd, "rm -f '%s' && echo removed: '%s'", dest, dest);
	else return fprintf(stderr, "usage: build [build|install|uninstall]\n"), 1;
	return system(cmd) ? 1 : 0;
}

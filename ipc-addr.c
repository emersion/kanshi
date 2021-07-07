#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include "ipc.h"

int get_ipc_address(char *address, size_t size) {
	const char *wayland_display = getenv("WAYLAND_DISPLAY");
	const char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!wayland_display || !wayland_display[0]) {
		fprintf(stderr, "WAYLAND_DISPLAY is not set\n");
		return -1;
	}
	if (!xdg_runtime_dir || !xdg_runtime_dir[0]) {
		fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
		return -1;
	}

	return snprintf(address, size, "unix:%s/fr.emersion.kanshi.%s",
			xdg_runtime_dir, wayland_display);
}

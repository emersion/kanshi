#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

#include "ipc.h"

bool check_env(void) {
	char *wayland_socket = getenv("WAYLAND_DISPLAY");
	char *xdg_runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!wayland_socket || !wayland_socket[0]) {
		fprintf(stderr, "WAYLAND_DISPLAY is not set\n");
		return false;
	}
	if (!xdg_runtime_dir || !xdg_runtime_dir[0]) {
		fprintf(stderr, "XDG_RUNTIME_DIR is not set\n");
		return false;
	}

	return true;
}

#ifndef KANSHI_IPC_H
#define KANSHI_IPC_H

#include <stdio.h>
#include <stdlib.h>

static int get_ipc_address(char *address, size_t size) {
	const char *socket = getenv("WAYLAND_DISPLAY");
	if (socket == NULL) {
		socket = "wayland-0";
	}
	return snprintf(address, size, "unix:@fr.emersion.kanshi.%s", socket);
}

#endif

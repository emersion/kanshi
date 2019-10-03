#ifndef KANSHI_IPC_H
#define KANSHI_IPC_H

#include <stdio.h>
#include <stdlib.h>
#include <varlink.h>

#define varlink_return_if_fail_v(_val, _fmt, ...) { \
	long _result = (_val); \
	if (_result != 0) { \
			fprintf(stderr, _fmt ": %s\n", __VA_ARGS__, \
					varlink_error_string(-_result)); \
			return EXIT_FAILURE; \
	} \
}

#define varlink_return_if_fail(_val) \
	varlink_return_if_fail_v(_val, "%s failed", #_val)

static int get_ipc_address(char *address, size_t size) {
	const char *socket = getenv("WAYLAND_DISPLAY");
	if (socket == NULL) {
		socket = "wayland-0";
	}
	return snprintf(address, size, "unix:@fr.emersion.kanshi.%s", socket);
}

#endif

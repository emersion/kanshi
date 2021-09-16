#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <varlink.h>

#include "ipc.h"

#define PREFIX "fr.emersion.kanshi."

static void usage(const char *progname) {
	fprintf(stderr, "Usage: %s [command]\n"
			"Accepted commands:\n"
			"  reload - reload the config file\n"
			"  set-profile <profile name> - try to apply a named profile\n",
			progname);
}

static long reload_callback(VarlinkConnection *connection, const char *error,
		VarlinkObject *parameters, uint64_t flags, void *userdata) {
	return varlink_connection_close(connection);
}

static int set_blocking(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		fprintf(stderr, "fnctl F_GETFL failed: %s\n", strerror(errno));
		return -1;
	}
	flags &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		fprintf(stderr, "fnctl F_SETFL failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

int wait_for_event(VarlinkConnection *connection) {
	int fd = varlink_connection_get_fd(connection);
	if (set_blocking(fd) != 0) {
		return -1;
	}

	while (!varlink_connection_is_closed(connection)) {
		uint32_t events = varlink_connection_get_events(connection);
		long result = varlink_connection_process_events(connection, events);
		if (result != 0) {
			fprintf(stderr, "varlink_connection_process_events failed: %s\n",
					varlink_error_string(-result));
			return -1;
		}
	}
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage(argv[0]);
		return EXIT_SUCCESS;
	}

	VarlinkConnection *connection;
	char address[PATH_MAX];
	if (get_ipc_address(address, sizeof(address)) < 0) {
		return EXIT_FAILURE;
	}
	if (varlink_connection_new(&connection, address) != 0) {
		fprintf(stderr, "Couldn't connect to kanshi at %s.\n"
				"Is the kanshi daemon running?\n", address);
		return EXIT_FAILURE;
	}

	const char *method_name = NULL;
	VarlinkObject *parameter = NULL;
	if (strcmp(argv[1], "reload") == 0) {
		method_name = PREFIX "Reload";
	} else if (strcmp(argv[1], "set-profile") == 0) {
		if (argc != 3) {
			return EXIT_FAILURE;
		}
		method_name = PREFIX "SetProfile";
		varlink_object_new(&parameter);
		varlink_object_set_string(parameter, "profile", argv[2]);
	}
	if (method_name) {
		long result = varlink_connection_call(connection,
				method_name, parameter, 0, reload_callback, NULL);
		if (result != 0) {
			fprintf(stderr, "varlink_connection_call failed: %s\n",
					varlink_error_string(-result));
			return EXIT_FAILURE;
		}
		return wait_for_event(connection);
	}

	fprintf(stderr, "invalid command: %s\n", argv[1]);
	usage(argv[0]);
	return EXIT_FAILURE;
}

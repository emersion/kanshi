
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "ipc.h"

static void usage(const char *progname) {
	fprintf(stderr, "Usage: %s [command]\n"
			"Accepted commands:\n"
			"  reload - reload the config file\n",
			progname);
}

static long reload_callback(VarlinkConnection *connection, const char *error,
		VarlinkObject *parameters, uint64_t flags, void *userdata) {
	bool *done = userdata;
	*done = true;
	return 0;
}

#define ARRAY_LENGTH(_arr) (sizeof(_arr) / sizeof(*(_arr)))

int wait_for_event(VarlinkConnection *connection, bool *done) {
	struct pollfd fds[1];
	fds[0].fd = varlink_connection_get_fd(connection);
	fds[0].events = varlink_connection_get_events(connection);

	while (!*done) {
		int ret;
		do {
			ret = poll(fds, ARRAY_LENGTH(fds), -1);
		} while (ret == -1 && errno == EINTR);
		if (ret == -1) {
			return EXIT_FAILURE;
		}
		varlink_return_if_fail(varlink_connection_process_events(connection,
					fds[0].revents));
	}
	return EXIT_SUCCESS;
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
	get_ipc_address(address, sizeof(address));
	if (varlink_connection_new(&connection, address) < 0) {
		fprintf(stderr, "Couldn't connect to kanshi at %s.\n"
				"Is the kanshi daemon running?\n", address);
		return EXIT_FAILURE;
	}
	if (strcmp(argv[1], "reload") == 0) {
		bool done = false;
		varlink_return_if_fail(varlink_connection_call(connection,
					"fr.emersion.kanshi.Reload", NULL, 0, reload_callback, &done));
		int ret = wait_for_event(connection, &done);
		if (!ret) {
			varlink_return_if_fail(varlink_connection_close(connection));
		}
		return ret;
	}
	fprintf(stderr, "invalid command: %s\n", argv[1]);
	usage(argv[0]);
	return EXIT_FAILURE;
}

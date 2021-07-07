#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kanshi.h"

enum readfds_type {
	FD_WAYLAND,
	FD_COUNT,
};

int kanshi_main_loop(struct kanshi_state *state) {
	struct pollfd readfds[FD_COUNT] = {0};
	readfds[FD_WAYLAND].fd = wl_display_get_fd(state->display);
	readfds[FD_WAYLAND].events = POLLIN;

	while (state->running) {
		while (wl_display_prepare_read(state->display) != 0) {
			if (wl_display_dispatch_pending(state->display) == -1) {
				return EXIT_FAILURE;
			}
		}

		int ret;

		while (true) {
			ret = wl_display_flush(state->display);
			if (ret != -1 || errno != EAGAIN) {
				break;
			}
		}

		if (ret < 0 && errno != EPIPE) {
			goto read_error;
		}

		do {
			ret = poll(readfds, sizeof(readfds) / sizeof(readfds[0]), -1);
		} while (ret == -1 && errno == EINTR);
		/* will only be -1 if errno wasn't EINTR */
		if (ret == -1) {
			goto read_error;
		}

		if (wl_display_read_events(state->display) == -1) {
			return EXIT_FAILURE;
		}

		if (wl_display_dispatch_pending(state->display) == -1) {
			return EXIT_FAILURE;
		}
	}

	return EXIT_SUCCESS;

read_error:
	wl_display_cancel_read(state->display);
	return EXIT_FAILURE;
}

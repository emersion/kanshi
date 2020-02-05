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

#ifdef KANSHI_HAS_VARLINK
#include <varlink.h>
#endif

#define ARRAY_LENGTH(_arr) (sizeof(_arr) / sizeof(*(_arr)))

static int do_poll(struct pollfd *fds, nfds_t nfds) {
	int ret;
	do {
		ret = poll(fds, nfds, -1);
	} while (ret == -1 && errno == EINTR);
	return ret;
}

static int set_pipe_flags(int fd) {
	int flags = fcntl(fd, F_GETFL);
	if (flags == -1) {
		fprintf(stderr, "fnctl F_GETFL failed: %s\n", strerror(errno));
		return -1;
	}
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) == -1) {
		fprintf(stderr, "fnctl F_SETFL failed: %s\n", strerror(errno));
		return -1;
	}
	flags = fcntl(fd, F_GETFD);
	if (flags == -1) {
		fprintf(stderr, "fnctl F_GETFD failed: %s\n", strerror(errno));
		return -1;
	}
	flags |= O_CLOEXEC;
	if (fcntl(fd, F_SETFD, flags) == -1) {
		fprintf(stderr, "fnctl F_SETFD failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int signal_pipefds[2];

static void signal_handler(int signum) {
	if (write(signal_pipefds[1], &signum, sizeof(signum)) == -1) {
		_exit(signum | 0x80);
	}
}

enum readfds_type {
	FD_WAYLAND = 0,
	FD_SIGNAL = 1,
#ifdef KANSHI_HAS_VARLINK
	FD_VARLINK = 2,
#endif
	FD_COUNT
};

int kanshi_main_loop(struct kanshi_state *state) {
	if (pipe(signal_pipefds) == -1) {
		fprintf(stderr, "read from signalfd failed: %s\n", strerror(errno));
		return EXIT_FAILURE;
	}
	if (set_pipe_flags(signal_pipefds[0]) == -1) {
		return EXIT_FAILURE;
	}
	if (set_pipe_flags(signal_pipefds[1]) == -1) {
		return EXIT_FAILURE;
	}

	struct sigaction action;
	sigfillset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = signal_handler;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGQUIT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGHUP, &action, NULL);

	struct pollfd readfds[FD_COUNT];
	readfds[FD_WAYLAND].fd = wl_display_get_fd(state->display);
	readfds[FD_WAYLAND].events = POLLIN;
	readfds[FD_SIGNAL].fd = signal_pipefds[0];
	readfds[FD_SIGNAL].events = POLLIN;
#ifdef KANSHI_HAS_VARLINK
	readfds[FD_VARLINK].fd = varlink_service_get_fd(state->service);
	readfds[FD_VARLINK].events = POLLIN;
#endif

	struct pollfd writefds[1];
	writefds[0].fd = readfds[FD_WAYLAND].fd;
	writefds[0].events = POLLOUT;

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

			if (do_poll(writefds, ARRAY_LENGTH(writefds)) == -1) {
				goto read_error;
			}
		}

		if (ret < 0 && errno != EPIPE) {
			goto read_error;
		}

		if (do_poll(readfds, ARRAY_LENGTH(readfds)) == -1) {
			goto read_error;
		}

		if (wl_display_read_events(state->display) == -1) {
			return EXIT_FAILURE;
		}

#ifdef KANSHI_HAS_VARLINK
		if (readfds[FD_VARLINK].revents & POLLIN) {
			long result = varlink_service_process_events(state->service);
			if (result != 0) {
				fprintf(stderr, "varlink_service_process_events failed: %s\n",
						varlink_error_string(-result));
				return EXIT_FAILURE;
			}
		}
#endif

		if (readfds[FD_SIGNAL].revents & POLLIN) {
			for (;;) {
				int signum;
				ssize_t s
					= read(readfds[FD_SIGNAL].fd, &signum, sizeof(signum));
				if (s == 0) {
					break;
				}
				if (s < 0) {
					if (errno == EAGAIN) {
						break;
					}
					fprintf(stderr, "read from signal pipe failed: %s\n",
							strerror(errno));
					return EXIT_FAILURE;
				}
				if (s < (ssize_t) sizeof(signum)) {
					fprintf(stderr, "read too few bytes from signal pipe\n");
					return EXIT_FAILURE;
				}
				switch (signum) {
				case SIGHUP:
					kanshi_reload_config(state);
					break;
				default:
					return signum | 0x80;
				}
			}
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

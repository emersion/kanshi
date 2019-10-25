#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#include "config.h"
#include "kanshi.h"
#include "parser.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

#ifdef KANSHI_HAS_VARLINK
#include <varlink.h>
#include "ipc.h"
#endif

#define HEADS_MAX 64

#define ARRAY_LENGTH(_arr) (sizeof(_arr) / sizeof(*(_arr)))

static bool match_profile_output(struct kanshi_profile_output *output,
		struct kanshi_head *head) {
	// TODO: improve vendor/model/serial matching
	return strcmp(output->name, "*") == 0 ||
		strcmp(output->name, head->name) == 0 ||
		(strchr(output->name, ' ') != NULL &&
		strstr(head->description, output->name) != NULL);
}

static bool match_profile(struct kanshi_state *state,
		struct kanshi_profile *profile,
		struct kanshi_profile_output *matches[static HEADS_MAX]) {
	if (wl_list_length(&profile->outputs) != wl_list_length(&state->heads)) {
		return false;
	}

	memset(matches, 0, HEADS_MAX * sizeof(struct kanshi_head *));

	// Wildcards are stored at the end of the list, so those will be matched
	// last
	struct kanshi_profile_output *profile_output;
	wl_list_for_each(profile_output, &profile->outputs, link) {
		bool output_matched = false;
		ssize_t i = -1;
		struct kanshi_head *head;
		wl_list_for_each(head, &state->heads, link) {
			i++;

			if (matches[i] != NULL) {
				continue; // already matched
			}

			if (match_profile_output(profile_output, head)) {
				matches[i] = profile_output;
				output_matched = true;
				break;
			}
		}

		if (!output_matched) {
			return false;
		}
	}

	return true;
}

static struct kanshi_profile *match(struct kanshi_state *state,
		struct kanshi_profile_output *matches[static HEADS_MAX]) {
	struct kanshi_profile *profile;
	wl_list_for_each(profile, &state->config->profiles, link) {
		if (match_profile(state, profile, matches)) {
			return profile;
		}
	}
	return NULL;
}


static void exec_command(char *cmd) {
	pid_t pid, child;
	if ((pid = fork()) == 0) {
		setsid();
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);

		struct sigaction action;
		sigfillset(&action.sa_mask);
		action.sa_flags = 0;
		action.sa_handler = SIG_DFL;
		sigaction(SIGINT, &action, NULL);
		sigaction(SIGQUIT, &action, NULL);
		sigaction(SIGTERM, &action, NULL);
		sigaction(SIGHUP, &action, NULL);

		if ((child = fork()) == 0) {
			execl("/bin/sh", "/bin/sh", "-c", cmd, (void *)NULL);
			fprintf(stderr, "Executing command '%s' failed: %s", cmd, strerror(errno));
			exit(-1);
		}
		if (child < 0) {
			fprintf(stderr, "Impossible to fork a new process to execute"
					" command '%s': %s", cmd, strerror(errno));
			exit(0);
		}

		// Try to give some meaningful information on the command success
		int wstatus;
		if (waitpid(child, &wstatus, 0) != child) {
			perror("waitpid");
			exit(0);
		}
		if (WIFEXITED(wstatus)) {
			fprintf(stderr, "Command '%s' returned with exit status %d.\n",
					cmd, WEXITSTATUS(wstatus));
		} else {
			fprintf(stderr, "Command '%s' was killed, aborted or disappeared"
					" in dire circumstances.\n", cmd);
		}
		exit(0); // Close child process
	}

	if (pid < 0) {
		perror("Impossible to fork a new process");
	}
}

static void execute_profile_commands(struct kanshi_profile *profile) {
	struct kanshi_profile_command *command;
	wl_list_for_each(command, &profile->commands, link) {
		fprintf(stderr, "Running command '%s'\n", command->command);
		exec_command(command->command);
	}
}

static void config_handle_succeeded(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);
	fprintf(stderr, "running commands for configuration '%s'\n", pending->profile->name);
	execute_profile_commands(pending->profile);
	fprintf(stderr, "configuration for profile '%s' applied\n",
			pending->profile->name);
	pending->state->current_profile = pending->profile;
	free(pending);
}

static void config_handle_failed(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);
	fprintf(stderr, "failed to apply configuration for profile '%s'\n",
			pending->profile->name);
	free(pending);
}

static void config_handle_cancelled(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);
	// Wait for new serial
	fprintf(stderr, "configuration for profile '%s' cancelled, retrying\n",
			pending->profile->name);
	free(pending);
}

static const struct zwlr_output_configuration_v1_listener config_listener = {
	.succeeded = config_handle_succeeded,
	.failed = config_handle_failed,
	.cancelled = config_handle_cancelled,
};

static bool match_refresh(const struct kanshi_mode *mode, int refresh) {
	int v = refresh - mode->refresh;
	return abs(v) < 50;
}

static struct kanshi_mode *match_mode(struct kanshi_head *head,
		int width, int height, int refresh) {
	struct kanshi_mode *mode;
	struct kanshi_mode *last_match = NULL;

	wl_list_for_each(mode, &head->modes, link) {
		if (mode->width != width || mode->height != height) {
			continue;
		}

		if (refresh) {
			if (match_refresh(mode, refresh)) {
				return mode;
			}
		} else {
			if (!last_match || mode->refresh > last_match->refresh) {
				last_match = mode;
			}
		}
	}

	return last_match;
}

static void apply_profile(struct kanshi_state *state,
		struct kanshi_profile *profile,
		struct kanshi_profile_output **matches) {
	if (state->pending_profile == profile || state->current_profile == profile) {
		return;
	}

	fprintf(stderr, "applying profile '%s'\n", profile->name);

	struct kanshi_pending_profile *pending = calloc(1, sizeof(*pending));
	pending->state = state;
	pending->profile = profile;
	state->pending_profile = profile;

	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(state->output_manager,
		state->serial);
	zwlr_output_configuration_v1_add_listener(config, &config_listener, pending);

	ssize_t i = -1;
	struct kanshi_head *head;
	wl_list_for_each(head, &state->heads, link) {
		i++;
		struct kanshi_profile_output *profile_output = matches[i];

		fprintf(stderr, "applying profile output '%s' on connected head '%s'\n",
			profile_output->name, head->name);

		bool enabled = head->enabled;
		if (profile_output->fields & KANSHI_OUTPUT_ENABLED) {
			enabled = profile_output->enabled;
		}

		if (!enabled) {
			zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
			continue;
		}

		struct zwlr_output_configuration_head_v1 *config_head =
			zwlr_output_configuration_v1_enable_head(config, head->wlr_head);
		if (profile_output->fields & KANSHI_OUTPUT_MODE) {
			// TODO: support custom modes
			struct kanshi_mode *mode = match_mode(head,
				profile_output->mode.width, profile_output->mode.height,
				profile_output->mode.refresh);
			if (mode == NULL) {
				fprintf(stderr,
					"output '%s' doesn't support mode '%dx%d@%fHz'\n",
					head->name,
					profile_output->mode.width, profile_output->mode.height,
					(float)profile_output->mode.refresh / 1000);
				goto error;
			}
			zwlr_output_configuration_head_v1_set_mode(config_head,
				mode->wlr_mode);
		}
		if (profile_output->fields & KANSHI_OUTPUT_POSITION) {
			zwlr_output_configuration_head_v1_set_position(config_head,
				profile_output->position.x, profile_output->position.y);
		}
		if (profile_output->fields & KANSHI_OUTPUT_SCALE) {
			zwlr_output_configuration_head_v1_set_scale(config_head,
				wl_fixed_from_double(profile_output->scale));
		}
		if (profile_output->fields & KANSHI_OUTPUT_TRANSFORM) {
			zwlr_output_configuration_head_v1_set_transform(config_head,
				profile_output->transform);
		}
	}

	zwlr_output_configuration_v1_apply(config);

	wl_display_roundtrip(state->display);
	return;

error:
	zwlr_output_configuration_v1_destroy(config);
}


static void mode_handle_size(void *data, struct zwlr_output_mode_v1 *wlr_mode,
		int32_t width, int32_t height) {
	struct kanshi_mode *mode = data;
	mode->width = width;
	mode->height = height;
}

static void mode_handle_refresh(void *data,
		struct zwlr_output_mode_v1 *wlr_mode, int32_t refresh) {
	struct kanshi_mode *mode = data;
	mode->refresh = refresh;
}

static void mode_handle_preferred(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct kanshi_mode *mode = data;
	mode->preferred = true;
}

static void mode_handle_finished(void *data,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct kanshi_mode *mode = data;
	wl_list_remove(&mode->link);
	zwlr_output_mode_v1_destroy(mode->wlr_mode);
	free(mode);
}

static const struct zwlr_output_mode_v1_listener mode_listener = {
	.size = mode_handle_size,
	.refresh = mode_handle_refresh,
	.preferred = mode_handle_preferred,
	.finished = mode_handle_finished,
};

static void head_handle_name(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *name) {
	struct kanshi_head *head = data;
	head->name = strdup(name);
}

static void head_handle_description(void *data,
		struct zwlr_output_head_v1 *wlr_head, const char *description) {
	struct kanshi_head *head = data;
	head->description = strdup(description);
}

static void head_handle_physical_size(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t width, int32_t height) {
	struct kanshi_head *head = data;
	head->phys_width = width;
	head->phys_height = height;
}

static void head_handle_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct kanshi_head *head = data;

	struct kanshi_mode *mode = calloc(1, sizeof(*mode));
	mode->head = head;
	mode->wlr_mode = wlr_mode;
	wl_list_insert(head->modes.prev, &mode->link);

	zwlr_output_mode_v1_add_listener(wlr_mode, &mode_listener, mode);
}

static void head_handle_enabled(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t enabled) {
	struct kanshi_head *head = data;
	head->enabled = !!enabled;
	if (!enabled) {
		head->mode = NULL;
	}
}

static void head_handle_current_mode(void *data,
		struct zwlr_output_head_v1 *wlr_head,
		struct zwlr_output_mode_v1 *wlr_mode) {
	struct kanshi_head *head = data;
	struct kanshi_mode *mode;
	wl_list_for_each(mode, &head->modes, link) {
		if (mode->wlr_mode == wlr_mode) {
			head->mode = mode;
			return;
		}
	}
	fprintf(stderr, "received unknown current_mode\n");
	head->mode = NULL;
}

static void head_handle_position(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
	struct kanshi_head *head = data;
	head->x = x;
	head->y = y;
}

static void head_handle_transform(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t transform) {
	struct kanshi_head *head = data;
	head->transform = transform;
}

static void head_handle_scale(void *data,
		struct zwlr_output_head_v1 *wlr_head, wl_fixed_t scale) {
	struct kanshi_head *head = data;
	head->scale = wl_fixed_to_double(scale);
}

static void head_handle_finished(void *data,
		struct zwlr_output_head_v1 *wlr_head) {
	struct kanshi_head *head = data;
	wl_list_remove(&head->link);
	zwlr_output_head_v1_destroy(head->wlr_head);
	free(head->name);
	free(head->description);
	free(head);
}

static const struct zwlr_output_head_v1_listener head_listener = {
	.name = head_handle_name,
	.description = head_handle_description,
	.physical_size = head_handle_physical_size,
	.mode = head_handle_mode,
	.enabled = head_handle_enabled,
	.current_mode = head_handle_current_mode,
	.position = head_handle_position,
	.transform = head_handle_transform,
	.scale = head_handle_scale,
	.finished = head_handle_finished,
};

static void output_manager_handle_head(void *data,
		struct zwlr_output_manager_v1 *manager,
		struct zwlr_output_head_v1 *wlr_head) {
	struct kanshi_state *state = data;

	struct kanshi_head *head = calloc(1, sizeof(*head));
	head->state = state;
	head->wlr_head = wlr_head;
	head->scale = 1.0;
	wl_list_init(&head->modes);
	wl_list_insert(&state->heads, &head->link);

	zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static bool try_apply_profiles(struct kanshi_state *state) {
	assert(wl_list_length(&state->heads) <= HEADS_MAX);
	// matches[i] gives the kanshi_profile_output for the i-th head
	struct kanshi_profile_output *matches[HEADS_MAX];
	struct kanshi_profile *profile = match(state, matches);
	if (profile != NULL) {
		apply_profile(state, profile, matches);
		return true;
	}
	fprintf(stderr, "no profile matched\n");
	return false;
}

static void output_manager_handle_done(void *data,
		struct zwlr_output_manager_v1 *manager, uint32_t serial) {
	struct kanshi_state *state = data;
	state->serial = serial;

	try_apply_profiles(state);
}

static void output_manager_handle_finished(void *data,
		struct zwlr_output_manager_v1 *manager) {
	// This space is intentionally left blank
}

static const struct zwlr_output_manager_v1_listener output_manager_listener = {
	.head = output_manager_handle_head,
	.done = output_manager_handle_done,
	.finished = output_manager_handle_finished,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct kanshi_state *state = data;

	if (strcmp(interface, zwlr_output_manager_v1_interface.name) == 0) {
		state->output_manager = wl_registry_bind(registry, name,
			&zwlr_output_manager_v1_interface, 1);
		zwlr_output_manager_v1_add_listener(state->output_manager,
			&output_manager_listener, state);
	}
}

static void registry_handle_global_remove(void *data,
		struct wl_registry *registry, uint32_t name) {
	// This space is intentionally left blank
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static struct kanshi_config *read_config(void) {
	const char config_filename[] = "kanshi/config";
	char config_path[PATH_MAX];
	const char *xdg_config_home = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (xdg_config_home != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/%s",
			xdg_config_home, config_filename);
	} else if (home != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/.config/%s",
			home, config_filename);
	} else {
		fprintf(stderr, "HOME not set\n");
		return NULL;
	}

	return parse_config(config_path);
}

static void destroy_config(struct kanshi_config *config) {
	struct kanshi_profile *profile, *tmp_profile;
	wl_list_for_each_safe(profile, tmp_profile, &config->profiles, link) {
		struct kanshi_profile_output *output, *tmp_output;
		wl_list_for_each_safe(output, tmp_output, &profile->outputs, link) {
			free(output->name);
			wl_list_remove(&output->link);
			free(output);
		}
		struct kanshi_profile_command *command, *tmp_command;
		wl_list_for_each_safe(command, tmp_command, &profile->commands, link) {
			free(command->command);
			wl_list_remove(&command->link);
			free(command);
		}
		wl_list_remove(&profile->link);
		free(profile);
	}
	free(config);
}

static bool reload_config(struct kanshi_state *state) {
	fprintf(stderr, "reloading config\n");
	struct kanshi_config *config = read_config();
	if (config != NULL) {
		destroy_config(state->config);
		state->config = config;
		state->pending_profile = NULL;
		state->current_profile = NULL;
		return try_apply_profiles(state);
	}
	return false;
}

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
	FD_WAYLAND		= 0,
	FD_SIGNAL		= 1,
#ifdef KANSHI_HAS_VARLINK
	FD_VARLINK		= 2,
#endif
	FD_COUNT
};

static int loop(struct kanshi_state *state) {
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
			varlink_return_if_fail(
					varlink_service_process_events(state->service));
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
					reload_config(state);
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

#ifdef KANSHI_HAS_VARLINK
long fr_emersion_kanshi_Reload(VarlinkService *service, VarlinkCall *call,
		VarlinkObject *parameters, uint64_t flags, void *userdata) {
	struct kanshi_state *state = userdata;
	reload_config(state);
	varlink_call_reply(call, NULL, 0);
	return 0;
}

VarlinkService *init_varlink(struct kanshi_state *state) {
	VarlinkService *service;
	char address[PATH_MAX];
	get_ipc_address(address, sizeof(address));
	if (varlink_service_new(&service,
			"emersion", "kanshi", "1.1", "https://wayland.emersion.fr/kanshi/",
			address, -1) < 0) {
		fprintf(stderr, "Couldn't start kanshi varlink service at %s.\n"
				"Is the kanshi daemon already running?\n", address);
		return NULL;
	}

	const char *interface = "interface fr.emersion.kanshi\n"
		"method Reload() -> ()";

	long result = varlink_service_add_interface(service, interface,
			"Reload", fr_emersion_kanshi_Reload, state,
			NULL);
	if (result != 0) {
		fprintf(stderr, "varlink_service_add_interface failed: %s\n",
				varlink_error_string(-result));
		varlink_service_free(service);
		return NULL;
	}

	return service;
}
#endif

int main(int argc, char *argv[]) {
	struct wl_display *display = NULL;

	struct kanshi_config *config = read_config();
	if (config == NULL) {
		return EXIT_FAILURE;
	}

	display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to connect to display\n");
		return EXIT_FAILURE;
	}

	struct kanshi_state state = {
		.running = true,
		.display = display,
		.config = config
	};
#ifdef KANSHI_HAS_VARLINK
	state.service = init_varlink(&state);
	if (state.service == NULL) {
		goto error;
	}
#endif
	wl_list_init(&state.heads);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (state.output_manager == NULL) {
		fprintf(stderr, "compositor doesn't support "
			"wlr-output-management-unstable-v1\n");
		goto error;
	}

	int ret = loop(&state);
	goto done;

error:
	ret = EXIT_FAILURE;
done:
#ifdef KANSHI_HAS_VARLINK
	if (state.service) {
		varlink_service_free(state.service);
	}
#endif
	wl_display_disconnect(display);

	return ret;
}

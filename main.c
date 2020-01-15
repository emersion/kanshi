#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>

#include "config.h"
#include "kanshi.h"
#include "parser.h"
#include "wlr-output-management-unstable-v1-client-protocol.h"

char const *program_name;
char const USAGE[] =
"%s [-1] [-p <PROFILE>]\n";

static bool try_match_output_to_head(struct kanshi_profile_output *output,
		struct kanshi_head *head) {
	// TODO: improve vendor/model/serial matching
	return strcmp(output->name, "*") == 0 ||
		strcmp(output->name, head->name) == 0 ||
		(strchr(output->name, ' ') != NULL &&
		strstr(head->description, output->name) != NULL);
}

// Return true, if all heads in the `state` could be matched to an output in
// the `profile`.
//
// After a successfull call, `pprofile_outputs` will refer to the matched profiles
// for one-one head in the same order.
static bool try_match_profile(struct kanshi_state *state,
		struct kanshi_profile *profile,
		struct kanshi_profile_output ***pprofile_outputs) {
	if (*pprofile_outputs == NULL) {
		size_t const num_heads = wl_list_length(&state->heads);
		*pprofile_outputs = malloc(num_heads * sizeof **pprofile_outputs);
		if (*pprofile_outputs == NULL) {
			log_error("failed to allocate memory");
			return false;
		}
	}
	struct kanshi_profile_output **profile_outputs = *pprofile_outputs;

	struct kanshi_head *head;
	wl_list_for_each(head, &state->heads, link) {
		// Try match heads in the order of outputs in the profile.
		struct kanshi_profile_output *profile_output;
		wl_list_for_each(profile_output, &profile->outputs, link) {
			if (try_match_output_to_head(profile_output, head)) {
				goto head_matched;
			}
		}
		// None of the outputs in the profile matched on this head.
		return false;

	head_matched:
		*profile_outputs++ = profile_output;
	}

	return true;
}


static struct kanshi_profile *find_profile_byname(struct kanshi_state *state, char *name) {
	struct kanshi_profile *profile;
	wl_list_for_each(profile, &state->config->profiles, link) {
		if (strcmp(profile->name, name) == 0) {
			return profile;
		}
	}
	return NULL;
}

static void exec_command(char *cmd) {
	pid_t child;
	if ((child = fork()) <= 0) {
		if (child < 0) {
			log_errorf("cannot execute command ‘%s’: %s",
					cmd, strerror(errno));
		}
		// Return from parent.
		return;
	}

	if ((child = fork()) == 0) {
		const char *shell = getenv("SHELL");
		if (shell == NULL)
			shell = "/bin/sh";

		// Reset signal mask.
		sigset_t set;
		sigemptyset(&set);
		sigprocmask(SIG_SETMASK, &set, NULL);

		execl(shell, shell, "-c", cmd, NULL);
		log_errorf("cannot execute command ‘%s’: %s",
				cmd, strerror(errno));
		_exit(127);
	}
	if (child < 0) {
		log_errorf("cannot execute command ‘%s’: %s",
				cmd, strerror(errno));
		_exit(EXIT_FAILURE);
	}

	// Try to give some meaningful information on the command success
	int wstatus;
	if (waitpid(child, &wstatus, 0) != child) {
		log_errorf("cannot wait for command ‘%s’: %s",
				cmd, strerror(errno));
		_exit(EXIT_FAILURE);
	}
	if (WIFEXITED(wstatus)) {
		if (WEXITSTATUS(wstatus) != EXIT_SUCCESS)
			log_errorf("command ‘%s’ terminated with exit status %d",
					cmd, WEXITSTATUS(wstatus));
	} else {
		log_errorf("command ‘%s‘ received %s",
				cmd, strsignal(WTERMSIG(wstatus)));
	}
	_exit(EXIT_SUCCESS);
}

static void execute_profile_commands(struct kanshi_profile *profile) {
	struct kanshi_profile_command *command;
	wl_list_for_each(command, &profile->commands, link) {
		exec_command(command->command);
	}
}

static void config_handle_succeeded(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);

	log_infof("profile ‘%s’ configured", pending->profile->name);

	execute_profile_commands(pending->profile);

	if (pending->state->configure_once) {
		exit(EXIT_SUCCESS);
	}

	free(pending);
}

static void config_handle_failed(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);

	log_errorf("profile ‘%s’ failed",
			pending->profile->name);

	if (pending->state->configure_once) {
		exit(EXIT_FAILURE);
	}

	free(pending);
}

static void config_handle_cancelled(void *data,
		struct zwlr_output_configuration_v1 *config) {
	struct kanshi_pending_profile *pending = data;
	zwlr_output_configuration_v1_destroy(config);

	// Wait for new serial
	log_errorf("profile ‘%s’ cancelled",
			pending->profile->name);

	if (pending->state->configure_once) {
		exit(EXIT_FAILURE);
	}

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

static int apply_output(struct zwlr_output_configuration_v1 *config,
		struct kanshi_profile_output *profile_output,
		struct kanshi_head *head,
		int dry_run) {

	bool enabled = head->enabled;
	if (profile_output->fields & KANSHI_OUTPUT_ENABLED) {
		enabled = profile_output->enabled;
	}

	bool changed = enabled != head->enabled;

	if (!enabled) {
		zwlr_output_configuration_v1_disable_head(config, head->wlr_head);
		return changed;
	}

	struct zwlr_output_configuration_head_v1 *config_head =
		zwlr_output_configuration_v1_enable_head(config, head->wlr_head);
	if (profile_output->fields & KANSHI_OUTPUT_MODE) {
		// TODO: support custom modes
		struct kanshi_mode *mode = match_mode(head,
			profile_output->mode.width, profile_output->mode.height,
			profile_output->mode.refresh);
		if (mode == NULL) {
			if (!dry_run) {
				log_errorf("head ‘%s’ doesn't support mode %dx%d @ %.6f Hz",
					head->name,
					profile_output->mode.width, profile_output->mode.height,
					(float)profile_output->mode.refresh / 1000);
			}
			return -1;
		}
		if (head->mode != mode) {
			changed = 1;
			zwlr_output_configuration_head_v1_set_mode(config_head,
				mode->wlr_mode);
		}
	}
	if ((profile_output->fields & KANSHI_OUTPUT_POSITION) &&
		(head->position.x != profile_output->position.x ||
		 head->position.y != profile_output->position.y)) {
		changed = 1;
		zwlr_output_configuration_head_v1_set_position(config_head,
			profile_output->position.x, profile_output->position.y);
	}
	if (profile_output->fields & KANSHI_OUTPUT_SCALE) {
		int head_scale = wl_fixed_from_double(head->scale);
		int new_scale = wl_fixed_from_double(profile_output->scale);
		if (head_scale != new_scale) {
			changed = 1;
			zwlr_output_configuration_head_v1_set_scale(config_head, new_scale);
		}
	}
	if ((profile_output->fields & KANSHI_OUTPUT_TRANSFORM) &&
		head->transform != profile_output->transform) {
		changed = 1;
		zwlr_output_configuration_head_v1_set_transform(config_head,
			profile_output->transform);
	}

	return changed;
}

// Configure output using `profile`.
static int apply_profile(struct kanshi_state *state,
		struct kanshi_profile *profile,
		struct kanshi_profile_output **profile_outputs,
		int dry_run) {
	struct kanshi_pending_profile *pending = calloc(1, sizeof(*pending));
	if (pending == NULL) {
		log_error("failed to allocate memory");
		return -1;
	}
	pending->state = state;
	pending->profile = profile;

	struct zwlr_output_configuration_v1 *config =
		zwlr_output_manager_v1_create_configuration(state->output_manager,
		state->serial);

	int ret = -1;
	bool any_changed = false;
	struct kanshi_head *head;
	wl_list_for_each(head, &state->heads, link) {
		struct kanshi_profile_output *profile_output = *profile_outputs++;

		switch (apply_output(config, profile_output, head, dry_run)) {
		case -1:
			// Error occurred.
			goto error;
		case 1:
			// Configuration changed.
			if (!any_changed) {
				any_changed = true;
				if (!dry_run) {
					log_infof("applying profile ‘%s’", profile->name);
				}
			}

			if (!dry_run) {
				log_infof("  output ‘%s’ -> head ‘%s’",
					profile_output->name, head->name);
			}
			break;
		}
	}

	if (any_changed) {
		ret = 1;
		if (!dry_run) {
			zwlr_output_configuration_v1_add_listener(config, &config_listener, pending);
			zwlr_output_configuration_v1_apply(config);
			return ret;
		}
	} else {
		ret = 0;
		log_info("  nothing to do");
	}

error:
	zwlr_output_configuration_v1_destroy(config);
	return ret;
}

// Return the first matching profile for a `setup`; or NULL, if there is no
// such profile.
static struct kanshi_profile *find_matching_profile(struct kanshi_state *state,
		struct kanshi_profile_output ***pprofile_outputs) {
	struct kanshi_profile *profile;
	wl_list_for_each(profile, &state->config->profiles, link) {
		if (try_match_profile(state, profile, pprofile_outputs)) {
			return profile;
		}
	}
	// No matching profile was found for this state.
	return NULL;
}

static struct kanshi_profile *find_current_profile(struct kanshi_state *state,
		struct kanshi_profile_output ***pprofile_outputs) {
	struct kanshi_profile *profile;
	wl_list_for_each(profile, &state->config->profiles, link) {
		if (try_match_profile(state, profile, pprofile_outputs) &&
		    apply_profile(state, profile, *pprofile_outputs, 1/*dry run*/) == 0) {
			return profile;
		}
	}
	// No matching profile was found for this state.
	return NULL;
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
	if (head == NULL) {
		log_error("failed to allocate memory");
		return;
	}
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
	log_error("received unknown current_mode");
	head->mode = NULL;
}

static void head_handle_position(void *data,
		struct zwlr_output_head_v1 *wlr_head, int32_t x, int32_t y) {
	struct kanshi_head *head = data;
	head->position.x = x;
	head->position.y = y;
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
	if (head == NULL) {
		log_error("failed to allocate memory");
		return;
	}
	head->state = state;
	head->wlr_head = wlr_head;
	head->scale = 1.0;
	wl_list_init(&head->modes);
	wl_list_insert(&state->heads, &head->link);

	zwlr_output_head_v1_add_listener(wlr_head, &head_listener, head);
}

static void output_manager_handle_done(void *data,
		struct zwlr_output_manager_v1 *manager, uint32_t serial) {
	struct kanshi_state *state = data;
	state->serial = serial;

	log_info("configuration changed");

	struct kanshi_profile_output **profile_outputs = NULL;
	struct kanshi_profile *profile;

	if (state->force_profile == NULL) {
		// Test if any of the profiles matches exactly the current
		// configuration.
		if ((profile = find_current_profile(state, &profile_outputs)) == NULL) {
			// Select one profile that can be applied to the
			// current configuration.
			profile = find_matching_profile(state, &profile_outputs);
		}
	} else {
		profile = find_profile_byname(state, state->force_profile);
		if (profile && !try_match_profile(state, profile, &profile_outputs)) {
			// Provided profile found but not available.
			profile = NULL;
			if (state->configure_once) {
				exit(EXIT_FAILURE);
			}
		}

	}

	if (profile != NULL) {
		log_infof("profile ‘%s’ matched", profile->name);
		if (apply_profile(state, profile, profile_outputs, 0) == 0 &&
			state->configure_once) {
			exit(EXIT_SUCCESS);
		}
	} else {
		log_error("no profile matched");
	}

	free(profile_outputs);
}

static void output_manager_handle_finished(void *data,
		struct zwlr_output_manager_v1 *manager) {
	// We handle changes at `output_manager_handle_done`, so nothing to do here.
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
	const char *prefix;
	if ((prefix = getenv("XDG_CONFIG_HOME")) != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/%s",
			prefix, config_filename);
	} else if ((prefix = getenv("HOME")) != NULL) {
		snprintf(config_path, sizeof(config_path), "%s/.config/%s",
			prefix, config_filename);
	} else {
		log_error("HOME not set");
		return NULL;
	}

	return parse_config(config_path);
}

int main(int argc, char *argv[]) {
	if ((program_name = strrchr(argv[0], '/')) != NULL)
		++program_name;
	else
		program_name = argv[0];

	char *opt_profile = NULL;
	int opt_once = 0;

	for (char c; (c = getopt (argc, argv, "p:1h")) != -1; ) {
		switch (c) {
		case 'p':
			opt_profile = strdup(optarg);
			break;
		case '1':
			opt_once = 1;
			break;
		case 'h':
			fprintf(stdout, USAGE, program_name);
			return EXIT_SUCCESS;
		default:
			log_error("invalid argument(s)");
			return EXIT_FAILURE;
		}
	}
	if (optind < argc) {
		log_error("extra argument(s)");
		return EXIT_FAILURE;
	}

	struct kanshi_config *config = read_config();
	if (config == NULL) {
		return EXIT_FAILURE;
	}

	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		log_error("cannot connect to display");
		return EXIT_FAILURE;
	}

	struct kanshi_state state = {
		.running = true,
		.config = config,
		.force_profile = opt_profile,
		.configure_once = opt_once,
	};
	wl_list_init(&state.heads);

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, &state);
	wl_display_dispatch(display);
	wl_display_roundtrip(display);

	if (state.output_manager == NULL) {
		log_error("compositor doesn't support "
			"wlr-output-management-unstable-v1");
		return EXIT_FAILURE;
	}

	while (state.running && wl_display_dispatch(display) != -1) {
		// This space intentionally left blank
	}

	return !state.running ? EXIT_SUCCESS : EXIT_FAILURE;
}

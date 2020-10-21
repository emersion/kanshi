#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <scfg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>

#include <wayland-client.h>

#include "config.h"
#include "parser.h"

static bool parse_int(int *dst, const char *str) {
	char *end;
	errno = 0;
	int v = strtol(str, &end, 10);
	if (errno != 0 || end[0] != '\0' || str[0] == '\0') {
		return false;
	}
	*dst = v;
	return true;
}

static bool parse_mode(struct kanshi_profile_output *output, char *str) {
	const char *width = strtok(str, "x");
	const char *height = strtok(NULL, "@");
	const char *refresh = strtok(NULL, "");

	if (width == NULL || height == NULL) {
		fprintf(stderr, "invalid output mode: missing width/height\n");
		return false;
	}

	if (!parse_int(&output->mode.width, width)) {
		fprintf(stderr, "invalid output mode: invalid width\n");
		return false;
	}
	if (!parse_int(&output->mode.height, height)) {
		fprintf(stderr, "invalid output mode: invalid height\n");
		return false;
	}

	if (refresh != NULL) {
		char *end;
		errno = 0;
		float v = strtof(refresh, &end);
		if (errno != 0 || (end[0] != '\0' && strcmp(end, "Hz") != 0) ||
				str[0] == '\0') {
			fprintf(stderr, "invalid output mode: invalid refresh rate\n");
			return false;
		}
		output->mode.refresh = v * 1000;
	}

	return true;
}

static bool parse_position(struct kanshi_profile_output *output, char *str) {
	const char *x = strtok(str, ",");
	const char *y = strtok(NULL, "");

	if (x == NULL || y == NULL) {
		fprintf(stderr, "invalid output position: missing x/y\n");
		return false;
	}

	if (!parse_int(&output->position.x, x)) {
		fprintf(stderr, "invalid output position: invalid x\n");
		return false;
	}
	if (!parse_int(&output->position.y, y)) {
		fprintf(stderr, "invalid output position: invalid y\n");
		return false;
	}

	return true;
}

static bool parse_float(float *dst, const char *str) {
	char *end;
	errno = 0;
	float v = strtof(str, &end);
	if (errno != 0 || end[0] != '\0' || str[0] == '\0') {
		return false;
	}
	*dst = v;
	return true;
}

static bool parse_transform(enum wl_output_transform *dst, const char *str) {
	if (strcmp(str, "normal") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_NORMAL;
	} else if (strcmp(str, "90") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_90;
	} else if (strcmp(str, "180") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_180;
	} else if (strcmp(str, "270") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_270;
	} else if (strcmp(str, "flipped") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_FLIPPED;
	} else if (strcmp(str, "flipped-90") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_FLIPPED_90;
	} else if (strcmp(str, "flipped-180") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_FLIPPED_180;
	} else if (strcmp(str, "flipped-270") == 0) {
		*dst = WL_OUTPUT_TRANSFORM_FLIPPED_270;
	} else {
		return false;
	}
	return true;
}

static struct kanshi_profile_output *parse_profile_output(
		struct scfg_directive *dir) {
	struct kanshi_profile_output *output = calloc(1, sizeof(*output));

	if (dir->params_len == 0) {
		fprintf(stderr, "directive 'output': expected at least one param\n");
		return NULL;
	}
	output->name = strdup(dir->params[0]);

	bool has_key = false;
	enum kanshi_output_field key;
	for (size_t i = 1; i < dir->params_len; i++) {
		char *param = dir->params[i];

		if (has_key) {
			char *value = param;
			switch (key) {
			case KANSHI_OUTPUT_MODE:
				if (!parse_mode(output, value)) {
					return NULL;
				}
				break;
			case KANSHI_OUTPUT_POSITION:
				if (!parse_position(output, value)) {
					return NULL;
				}
				break;
			case KANSHI_OUTPUT_SCALE:
				if (!parse_float(&output->scale, value)) {
					fprintf(stderr, "invalid output scale\n");
					return NULL;
				}
				break;
			case KANSHI_OUTPUT_TRANSFORM:
				if (!parse_transform(&output->transform, value)) {
					fprintf(stderr, "invalid output transform\n");
					return NULL;
				}
				break;
			default:
				assert(0);
			}
			has_key = false;
			output->fields |= key;
		} else {
			has_key = true;
			const char *key_str = param;
			if (strcmp(key_str, "enable") == 0) {
				output->enabled = true;
				output->fields |= KANSHI_OUTPUT_ENABLED;
				has_key = false;
			} else if (strcmp(key_str, "disable") == 0) {
				output->enabled = false;
				output->fields |= KANSHI_OUTPUT_ENABLED;
				has_key = false;
			} else if (strcmp(key_str, "mode") == 0) {
				key = KANSHI_OUTPUT_MODE;
			} else if (strcmp(key_str, "position") == 0) {
				key = KANSHI_OUTPUT_POSITION;
			} else if (strcmp(key_str, "scale") == 0) {
				key = KANSHI_OUTPUT_SCALE;
			} else if (strcmp(key_str, "transform") == 0) {
				key = KANSHI_OUTPUT_TRANSFORM;
			} else {
				fprintf(stderr,
					"unknown directive '%s' in profile output '%s'\n",
					key_str, output->name);
				return NULL;
			}
		}
	}

	return output;
}

static struct kanshi_profile_command *parse_profile_exec(
		struct scfg_directive *dir) {
	if (dir->params_len != 1) {
		fprintf(stderr, "directive 'exec': expected exactly one param\n");
		return NULL;
	}

	struct kanshi_profile_command *command = calloc(1, sizeof(*command));
	command->command = strdup(dir->params[0]);
	return command;
}

static struct kanshi_profile *parse_profile(struct scfg_directive *dir) {
	struct kanshi_profile *profile = calloc(1, sizeof(*profile));
	wl_list_init(&profile->outputs);
	wl_list_init(&profile->commands);

	if (dir->params_len > 1) {
		fprintf(stderr, "directive 'profile': expected zero or one param\n");
	}
	if (dir->params_len > 0) {
		profile->name = strdup(dir->params[0]);
	}

	// Use the bracket position to generate a default profile name
	if (profile->name == NULL) {
		// TODO
		/*char generated_name[100];
		int ret = snprintf(generated_name, sizeof(generated_name),
				"<anonymous at line %d, col %d>", parser->line, parser->col);
		if (ret >= 0) {
			profile->name = strdup(generated_name);
		} else*/ {
			profile->name = strdup("<anonymous>");
		}
	}

	for (size_t i = 0; i < dir->children.directives_len; i++) {
		struct scfg_directive *child = &dir->children.directives[i];

		if (strcmp(child->name, "output") == 0) {
			struct kanshi_profile_output *output =
				parse_profile_output(child);
			if (output == NULL) {
				return NULL;
			}
			// Store wildcard outputs at the end of the list
			if (strcmp(output->name, "*") == 0) {
				wl_list_insert(profile->outputs.prev, &output->link);
			} else {
				wl_list_insert(&profile->outputs, &output->link);
			}
		} else if (strcmp(child->name, "exec") == 0) {
			struct kanshi_profile_command *command = parse_profile_exec(dir);
			if (command == NULL) {
				return NULL;
			}
			// Insert commands at the end to preserve order
			wl_list_insert(profile->commands.prev, &command->link);
		} else {
			fprintf(stderr, "profile '%s': unknown directive '%s'\n",
				profile->name, dir->name);
			return NULL;
		}
	}

	return profile;
}

static bool parse_config_file(const char *path, struct kanshi_config *config);

static bool parse_include_command(struct scfg_directive *dir, struct kanshi_config *config) {
	if (dir->params_len != 1) {
		fprintf(stderr, "directive 'include': expected exactly one parameter\n");
		return false;
	}

	wordexp_t p;
	if (wordexp(dir->params[0], &p, WRDE_SHOWERR | WRDE_UNDEF) != 0) {
		fprintf(stderr, "Could not expand include path: '%s'\n", dir->params[0]);
		return false;
	}

	char **w = p.we_wordv;
	for (size_t idx = 0; idx < p.we_wordc; idx++) {
		if (!parse_config_file(w[idx], config)) {
			fprintf(stderr, "Could not parse included config: '%s'\n", w[idx]);
			wordfree(&p);
			return false;
		}
	}
	wordfree(&p);
	return true;
}

static bool _parse_config(struct scfg_block *block, struct kanshi_config *config) {
	for (size_t i = 0; i < block->directives_len; i++) {
		struct scfg_directive *dir = &block->directives[i];

		// TODO: support legacy syntax without a directive name somehow
		if (strcmp(dir->name, "profile") == 0) {
			struct kanshi_profile *profile = parse_profile(dir);
			if (!profile) {
				return false;
			}
			wl_list_insert(config->profiles.prev, &profile->link);
		} else if (strcmp(dir->name, "include") == 0) {
			if (!parse_include_command(dir, config)) {
				return false;
			}
		} else {
			fprintf(stderr, "unknown directive '%s'\n", dir->name);
			return false;
		}
	}

	return true;
}

static bool parse_config_file(const char *path, struct kanshi_config *config) {
	struct scfg_block block = {0};
	if (scfg_load_file(&block, path) != 0) {
		fprintf(stderr, "failed to parse config file\n");
		return false;
	}

	if (!_parse_config(&block, config)) {
		fprintf(stderr, "failed to parse config file\n");
		return false;
	}

	scfg_block_finish(&block);
	return true;
}

struct kanshi_config *parse_config(const char *path) {
	struct kanshi_config *config = calloc(1, sizeof(*config));
	if (config == NULL) {
		return NULL;
	}
	wl_list_init(&config->profiles);

	if (!parse_config_file(path, config)) {
		free(config);
		return NULL;
	}

	return config;
}

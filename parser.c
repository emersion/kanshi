#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "config.h"
#include "parser.h"

static const char *token_type_str(enum kanshi_token_type t) {
	switch (t) {
	case KANSHI_TOKEN_LBRACKET:
		return "'{'";
	case KANSHI_TOKEN_RBRACKET:
		return "'}'";
	case KANSHI_TOKEN_STR:
		return "string";
	case KANSHI_TOKEN_NEWLINE:
		return "newline";
	}
	assert(0);
}

static int parser_read_char(struct kanshi_parser *parser) {
	if (parser->next >= 0) {
		int ch = parser->next;
		parser->next = -1;
		return ch;
	}

	int ch = fgetc(parser->f);
	if (ch == EOF) {
		if (errno != 0) {
			fprintf(stderr, "fgetc failed: %s\n", strerror(errno));
		} else {
			return '\0';
		}
		return -1;
	}

	if (ch == '\n') {
		parser->line++;
		parser->col = 0;
	} else {
		parser->col++;
	}

	return ch;
}

static int parser_peek_char(struct kanshi_parser *parser) {
	int ch = parser_read_char(parser);
	parser->next = ch;
	return ch;
}

static bool parser_read_str(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_peek_char(parser);
		if (ch < 0) {
			return false;
		}

		if (isspace(ch) || ch == '{' || ch == '}') {
			parser->tok_str[parser->tok_str_len] = '\0';
			return true;
		}

		// Always keep enough room for a terminating NULL char
		if (parser->tok_str_len + 1 >= sizeof(parser->tok_str)) {
			fprintf(stderr, "string too long\n");
			return false;
		}
		parser->tok_str[parser->tok_str_len] = parser_read_char(parser);
		parser->tok_str_len++;
	}
}

static bool parser_next_token(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_read_char(parser);
		if (ch < 0) {
			return ch;
		}

		if (ch == '{') {
			parser->tok_type = KANSHI_TOKEN_LBRACKET;
			return true;
		} else if (ch == '}') {
			parser->tok_type = KANSHI_TOKEN_RBRACKET;
			return true;
		} else if (ch == '\n') {
			parser->tok_type = KANSHI_TOKEN_NEWLINE;
			return true;
		} else if (isspace(ch)) {
			continue;
		} else {
			parser->tok_type = KANSHI_TOKEN_STR;
			parser->tok_str[0] = ch;
			parser->tok_str_len = 1;
			return parser_read_str(parser);
		}
	}
}

static bool parser_expect_token(struct kanshi_parser *parser,
		enum kanshi_token_type want) {
	if (!parser_next_token(parser)) {
		return false;
	}
	if (parser->tok_type != want) {
		fprintf(stderr, "expected %s, got %s\n",
			token_type_str(want), token_type_str(parser->tok_type));
		return false;
	}
	return true;
}

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
		output->mode.refresh = v;
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
		struct kanshi_parser *parser) {
	struct kanshi_profile_output *output = calloc(1, sizeof(*output));

	if (!parser_expect_token(parser, KANSHI_TOKEN_STR)) {
		return NULL;
	}
	output->name = strdup(parser->tok_str);

	bool has_key = false;
	enum kanshi_output_field key;
	while (1) {
		if (!parser_next_token(parser)) {
			return NULL;
		}

		switch (parser->tok_type) {
		case KANSHI_TOKEN_STR:
			if (has_key) {
				char *value = parser->tok_str;
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
				const char *key_str = parser->tok_str;
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
			break;
		case KANSHI_TOKEN_NEWLINE:
			return output;
		default:
			fprintf(stderr, "unexpected %s in output\n",
				token_type_str(parser->tok_type));
			return NULL;
		}
	}
}

static struct kanshi_profile *parse_profile(struct kanshi_parser *parser) {
	if (!parser_expect_token(parser, KANSHI_TOKEN_LBRACKET)) {
		return NULL;
	}

	struct kanshi_profile *profile = calloc(1, sizeof(*profile));
	wl_list_init(&profile->outputs);

	while (1) {
		if (!parser_next_token(parser)) {
			return NULL;
		}

		switch (parser->tok_type) {
		case KANSHI_TOKEN_RBRACKET:
			return profile;
		case KANSHI_TOKEN_STR:;
			const char *directive = parser->tok_str;
			if (strcmp(directive, "output") == 0) {
				struct kanshi_profile_output *output =
					parse_profile_output(parser);
				if (output == NULL) {
					return NULL;
				}
				wl_list_insert(&profile->outputs, &output->link);
			} else {
				fprintf(stderr, "unknown directive '%s' in profile\n",
					directive);
				return NULL;
			}
			break;
		case KANSHI_TOKEN_NEWLINE:
			break; // No-op
		default:
			fprintf(stderr, "unexpected %s in profile\n",
				token_type_str(parser->tok_type));
			return NULL;
		}
	}
}

static struct kanshi_config *_parse_config(struct kanshi_parser *parser) {
	struct kanshi_config *config = calloc(1, sizeof(*config));
	wl_list_init(&config->profiles);

	while (1) {
		int ch = parser_peek_char(parser);
		if (ch < 0) {
			return NULL;
		} else if (ch == 0) {
			return config;
		} else if (isspace(ch)) {
			parser_read_char(parser);
			continue;
		}

		struct kanshi_profile *profile = parse_profile(parser);
		if (!profile) {
			return NULL;
		}

		wl_list_insert(&config->profiles, &profile->link);
	}
}

struct kanshi_config *parse_config(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		fprintf(stderr, "failed to open file\n");
		return NULL;
	}

	struct kanshi_parser parser = {
		.f = f,
		.next = -1,
		.line = 1,
	};

	struct kanshi_config *config = _parse_config(&parser);
	fclose(f);
	if (config == NULL) {
		fprintf(stderr, "failed to parse config file: "
			"error on line %d, column %d\n", parser.line, parser.col);
		return NULL;
	}

	return config;
}

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
#include "kanshi.h"
#include "parser.h"

#define log_cfgerrorf(msg, ...) log_errorf("config: " msg, __VA_ARGS__)
#define log_cfgerror(msg) log_error("config: " msg)

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
			log_cfgerrorf("failed to read file: %s", strerror(errno));
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

static bool parser_append_tok_ch(struct kanshi_parser *parser, char ch) {
	// Always keep enough room for a terminating NULL char
	if (parser->tok_str_len + 1 >= sizeof parser->tok_str) {
		log_cfgerrorf("string too long: “%*s…”",
				(int)parser->tok_str_len, parser->tok_str);
		return false;
	}
	parser->tok_str[parser->tok_str_len] = ch;
	parser->tok_str_len++;
	return true;
}

static bool parser_read_quoted(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_read_char(parser);
		if (ch < 0) {
			return false;
		} else if (ch == '\0') {
			log_cfgerror("unterminated quoted string before end-of-file");
			return false;
		}

		if (ch == '"') {
			parser->tok_str[parser->tok_str_len] = '\0';
			return true;
		}

		if (!parser_append_tok_ch(parser, ch)) {
			return false;
		}
	}
}

static void parser_ignore_line(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_read_char(parser);
		if (ch < 0) {
			return;
		}

		if (ch == '\n' || ch == '\0') {
			return;
		}
	}
}

static bool parser_read_line(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_peek_char(parser);
		if (ch < 0) {
			return false;
		}

		if (ch == '\n' || ch == '\0') {
			parser->tok_str[parser->tok_str_len] = '\0';
			return true;
		}

		if (!parser_append_tok_ch(parser, parser_read_char(parser))) {
			return false;
		}
	}
}

static bool parser_read_str(struct kanshi_parser *parser) {
	while (1) {
		int ch = parser_peek_char(parser);
		if (ch < 0) {
			return false;
		}

		if (isspace(ch) || ch == '{' || ch == '}' || ch == '\0') {
			parser->tok_str[parser->tok_str_len] = '\0';
			return true;
		}

		if (!parser_append_tok_ch(parser, parser_read_char(parser))) {
			return false;
		}
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
		} else if (ch == '"') {
			parser->tok_type = KANSHI_TOKEN_STR;
			parser->tok_str_len = 0;
			return parser_read_quoted(parser);
		} else if (ch == '#') {
			parser_ignore_line(parser);
			parser->tok_type = KANSHI_TOKEN_NEWLINE;
			return true;
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
		log_cfgerrorf("expected %s, got %s",
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
		log_cfgerror("invalid output mode: must be in ‘<width>x<height>[@<rate>[Hz]]’ format");
		return false;
	}

	if (!parse_int(&output->mode.width, width)) {
		log_cfgerrorf("invalid output mode: ‘%s’ is not a valid integer for width",
				width);
		return false;
	}
	if (!parse_int(&output->mode.height, height)) {
		log_cfgerrorf("invalid output mode: ‘%s’ is not a valid integer for height",
				height);
		return false;
	}

	if (refresh != NULL) {
		char *end;
		errno = 0;
		float v = strtof(refresh, &end);
		if (errno != 0 || (end[0] != '\0' && strcmp(end, "Hz") != 0) ||
				str[0] == '\0') {
			log_cfgerrorf("invalid output mode: ‘%s’ is not valid for refresh rate",
					refresh);
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
		log_cfgerror("invalid output position: must be in ‘<x>,<y>’ format");
		return false;
	}

	if (!parse_int(&output->position.x, x)) {
		log_cfgerrorf("invalid output position: ‘%s’ is not a valid integer for x", x);
		return false;
	}
	if (!parse_int(&output->position.y, y)) {
		log_cfgerrorf("invalid output position: ‘%s’ is not a valid integer for y", y);
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
						log_cfgerrorf("‘%s’ is not valid for output scale",
								value);
						return NULL;
					}
					break;
				case KANSHI_OUTPUT_TRANSFORM:
					if (!parse_transform(&output->transform, value)) {
						log_cfgerrorf("‘%s’ is not valid for output transform",
								value);
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
				} else if (strcmp(key_str, "position") == 0 ||
					   strcmp(key_str, "pos") == 0) {
					key = KANSHI_OUTPUT_POSITION;
				} else if (strcmp(key_str, "scale") == 0) {
					key = KANSHI_OUTPUT_SCALE;
				} else if (strcmp(key_str, "transform") == 0) {
					key = KANSHI_OUTPUT_TRANSFORM;
				} else {
					log_cfgerrorf("unknown directive ‘%s’ in profile output ‘%s’",
						key_str, output->name);
					return NULL;
				}
			}
			break;
		case KANSHI_TOKEN_NEWLINE:
			return output;
		default:
			log_cfgerrorf("unexpected %s in output",
				token_type_str(parser->tok_type));
			return NULL;
		}
	}
}

static struct kanshi_profile_command *parse_profile_command(
		struct kanshi_parser *parser) {
	// Skip the 'exec' directive.
	if (!parser_expect_token(parser, KANSHI_TOKEN_STR)) {
		return NULL;
	}

	if (!parser_read_line(parser)) {
		return NULL;
	}

	if (parser->tok_str_len <= 0) {
		log_cfgerrorf("ignoring empty command in line %d",
			parser->line);
		return NULL;
	}

	struct kanshi_profile_command *command = calloc(1, sizeof(*command));
	command->command = strdup(parser->tok_str);
	return command;
}

static struct kanshi_profile *parse_profile(struct kanshi_parser *parser) {
	struct kanshi_profile *profile = calloc(1, sizeof(*profile));
	wl_list_init(&profile->outputs);
	wl_list_init(&profile->commands);

	// First parse an optional profile name
	parser->tok_str_len = 0;
	if (!parser_read_str(parser)) {
		log_cfgerrorf("expected new profile, got %s",
			token_type_str(parser->tok_type));
		return NULL;
	}
	profile->name = (parser->tok_str_len == 0) ? NULL : strdup(parser->tok_str);

	// Then parse the opening bracket
	if (!parser_expect_token(parser, KANSHI_TOKEN_LBRACKET)) {
		return NULL;
	}

	// Use the bracket position to generate a default profile name
	if (profile->name == NULL) {
		char generated_name[100];
		int ret = snprintf(generated_name, sizeof(generated_name),
				"<anonymous at line %d, column %d>", parser->line, parser->col);
		if (ret >= 0) {
			profile->name = strdup(generated_name);
		} else {
			profile->name = strdup("<anonymous>");
		}
	}

	// Parse the profile commands until the closing bracket
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
				// Store wildcard outputs at the end of the list
				if (strcmp(output->name, "*") == 0) {
					wl_list_insert(profile->outputs.prev, &output->link);
				} else {
					wl_list_insert(&profile->outputs, &output->link);
				}
			} else if (strcmp(directive, "exec") == 0) {
				struct kanshi_profile_command *command =
					parse_profile_command(parser);
				if (command == NULL) {
					return NULL;
				}
				// Insert commands at the end to preserve order
				wl_list_insert(profile->commands.prev, &command->link);
			} else {
				log_cfgerrorf("unknown directive ‘%s’ in profile ‘%s’",
					directive, profile->name);
				return NULL;
			}
			break;
		case KANSHI_TOKEN_NEWLINE:
			break; // No-op
		default:
			log_cfgerrorf("unexpected %s in profile ‘%s’",
				token_type_str(parser->tok_type), profile->name);
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
		} else if (ch == '#') {
			parser_ignore_line(parser);
			continue;
		} else if (isspace(ch)) {
			parser_read_char(parser);
			continue;
		}

		struct kanshi_profile *profile = parse_profile(parser);
		if (profile == NULL) {
			return NULL;
		}

		// Inset at the end to preserve ordering
		wl_list_insert(config->profiles.prev, &profile->link);
	}
}

struct kanshi_config *parse_config(const char *path) {
	FILE *f = fopen(path, "r");
	if (f == NULL) {
		log_cfgerrorf("%s: %s", path, strerror(errno));
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
		log_cfgerrorf("parse error at line %d, column %d",
				parser.line, parser.col);
		return NULL;
	}

	return config;
}

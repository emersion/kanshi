#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "parser.h"

const char *token_type_str(enum kanshi_token_type t) {
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

int parser_read_char(struct kanshi_parser *parser) {
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

int parser_peek_char(struct kanshi_parser *parser) {
	int ch = parser_read_char(parser);
	parser->next = ch;
	return ch;
}

bool parser_read_str(struct kanshi_parser *parser) {
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

bool parser_next_token(struct kanshi_parser *parser) {
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

bool parser_expect_token(struct kanshi_parser *parser,
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

struct kanshi_profile_output *parse_profile_output(struct kanshi_parser *parser) {
	struct kanshi_profile_output *output = calloc(1, sizeof(*output));

	if (!parser_expect_token(parser, KANSHI_TOKEN_STR)) {
		return NULL;
	}
	output->name = strdup(parser->tok_str);

	while (1) {
		if (!parser_next_token(parser)) {
			return NULL;
		}

		switch (parser->tok_type) {
		case KANSHI_TOKEN_STR:
			// TODO
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

struct kanshi_profile *parse_profile(struct kanshi_parser *parser) {
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
		case KANSHI_TOKEN_STR:
			if (strcmp(parser->tok_str, "output") == 0) {
				struct kanshi_profile_output *output =
					parse_profile_output(parser);
				if (output == NULL) {
					return NULL;
				}
				wl_list_insert(&profile->outputs, &output->link);
			} else {
				fprintf(stderr, "unexpected directive '%s' in profile\n",
					parser->tok_str);
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

struct kanshi_config *_parse_config(struct kanshi_parser *parser) {
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

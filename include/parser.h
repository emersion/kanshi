#ifndef KANSHI_PARSER_H
#define KANSHI_PARSER_H

#include <stdio.h>

struct kanshi_config;

enum kanshi_token_type {
	KANSHI_TOKEN_LBRACKET,
	KANSHI_TOKEN_RBRACKET,
	KANSHI_TOKEN_STR,
	KANSHI_TOKEN_NEWLINE,
};

struct kanshi_parser {
	FILE *f;
	int next;
	int line, col;

	enum kanshi_token_type tok_type;
	char tok_str[1024];
	size_t tok_str_len;
};

struct kanshi_config *parse_config(const char *path);

#endif

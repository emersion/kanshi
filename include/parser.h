#ifndef KANSHI_PARSER_H
#define KANSHI_PARSER_H

#include <stdio.h>

struct kanshi_config;

struct kanshi_config *parse_config(const char *path);

#endif

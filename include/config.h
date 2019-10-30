#ifndef KANSHI_CONFIG_H
#define KANSHI_CONFIG_H

#include <stdbool.h>
#include <wayland-client.h>

enum kanshi_output_field {
	KANSHI_OUTPUT_ENABLED = 1 << 0,
	KANSHI_OUTPUT_MODE = 1 << 1,
	KANSHI_OUTPUT_POSITION = 1 << 2,
	KANSHI_OUTPUT_SCALE = 1 << 3,
	KANSHI_OUTPUT_TRANSFORM = 1 << 4,
};

struct kanshi_profile_output {
	char *name;
	unsigned int fields; // enum kanshi_output_field
	struct wl_list link;

	bool enabled;
	struct {
		int width, height;
		int refresh; // mHz
	} mode;
	struct {
		int x, y;
	} position;
	float scale;
	enum wl_output_transform transform;
};

struct kanshi_profile_command {
	struct wl_list link;
	char *command;
};

struct kanshi_profile {
	struct wl_list link;
	char *name;
	// Wildcard outputs are stored at the end of the list
	struct wl_list outputs;
	struct wl_list commands;
};

struct kanshi_config {
	const char *home_dir;
	char *config_dir;
	struct wl_list profiles;
};

#endif

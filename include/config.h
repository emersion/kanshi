#ifndef KANSHI_CONFIG_H
#define KANSHI_CONFIG_H

#include <wayland-util.h>

struct kanshi_profile_output {
	struct wl_list link;
	char *name;
};

struct kanshi_profile {
	struct wl_list link;
	struct wl_list outputs;
};

struct kanshi_config {
	struct wl_list profiles;
};

#endif

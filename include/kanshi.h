#ifndef KANSHI_KANSHI_H
#define KANSHI_KANSHI_H

#include <stdbool.h>
#include <wayland-client.h>

struct zwlr_output_manager_v1;

struct kanshi_state;
struct kanshi_head;

struct kanshi_mode {
	struct kanshi_head *head;
	struct zwlr_output_mode_v1 *wlr_mode;
	struct wl_list link;

	int32_t width, height;
	int32_t refresh; // mHz
	bool preferred;
};

struct kanshi_head {
	struct kanshi_state *state;
	struct zwlr_output_head_v1 *wlr_head;
	struct wl_list link;

	char *name, *description;
	int32_t phys_width, phys_height; // mm
	struct wl_list modes;

	bool enabled;
	struct kanshi_mode *mode;
	struct {
		int32_t width, height;
		int32_t refresh;
	} custom_mode;
	int32_t x, y;
	enum wl_output_transform transform;
	double scale;
};

struct kanshi_state {
	bool running;
	struct wl_display *display;
	struct zwlr_output_manager_v1 *output_manager;
#if KANSHI_HAS_VARLINK
	struct VarlinkService *service;
#endif

	struct kanshi_config *config;
	const char *config_arg;

	struct wl_list heads;
	uint32_t serial;
	struct kanshi_profile *current_profile;
	struct kanshi_profile *pending_profile;
};

struct kanshi_pending_profile {
	struct kanshi_state *state;
	struct kanshi_profile *profile;
};

bool kanshi_reload_config(struct kanshi_state *state);

int kanshi_main_loop(struct kanshi_state *state);

#endif

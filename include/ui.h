#ifndef _WLPV_UI_H
#define _WLPV_UI_H
#include <nwl/nwl.h>

struct wlpavuo_state {
	struct wp_single_pixel_buffer_manager_v1 *sp_buffer_manager;
	bool use_pipewire;
	bool use_shm;
	bool no_seat;
	bool stdin_input;
	uint32_t width;
	uint32_t height;
	bool dynamic_height;
	char mode;
	uint32_t anchor;
	struct nwl_state nwl;
};

void wlpavuo_ui_input_pointer(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_pointer_event *event);
void wlpavuo_ui_input_keyboard(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_keyboard_event *event);
void wlpavuo_ui_input_stdin(struct nwl_state *state, void *data);
char wlpavuo_ui_run(struct nwl_surface *surface, cairo_t *cr);
void wlpavuo_ui_destroy(struct nwl_surface *surface);

#endif

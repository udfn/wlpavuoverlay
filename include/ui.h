#ifndef _WLPV_UI_H
#define _WLPV_UI_H
#include <nwl/nwl.h>
#include <nwl/surface.h>
#include <nwl/cairo.h>
struct wlpavuo_surface {
	struct nwl_surface main_surface;
	struct nwl_surface bg_surface;
	struct nwl_cairo_renderer cairo_renderer;
	union {
		struct wl_buffer *pixel_buffer;
		struct nwl_cairo_renderer cairo;
	} background_render;
	char bgrendered;
	uint32_t actual_width;
	uint32_t actual_height;
	struct wp_viewport *viewport;
	struct wlpavuo_ui *ui;
};

struct wlpavuo_state {
	struct wp_single_pixel_buffer_manager_v1 *sp_buffer_manager;
	struct wp_viewporter *viewporter;
	bool use_pipewire;
	bool no_seat;
	bool stdin_input;
	uint32_t width;
	uint32_t height;
	bool dynamic_height;
	char mode;
	uint32_t anchor;
	struct nwl_state nwl;
	struct wlpavuo_surface surface;
};

void wlpavuo_ui_input_pointer(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_pointer_event *event);
void wlpavuo_ui_input_keyboard(struct nwl_surface *surface, struct nwl_seat *seat, struct nwl_keyboard_event *event);
void wlpavuo_ui_input_stdin(struct nwl_state *state, uint32_t events, void *data);
void wlpavuo_ui_run(struct wlpavuo_surface *wlpsurface);
void wlpavuo_ui_destroy(struct nwl_surface *surface);

#endif

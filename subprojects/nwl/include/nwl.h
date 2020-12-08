#ifndef _NWL_NWL_H
#define _NWL_NWL_H
#define UNUSED(x) (void)(x)
#include <stdbool.h>
#include <EGL/egl.h>
#include <cairo/cairo.h>
#include <wayland-util.h>
#include "seat.h"

struct nwl_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_decoration_manager_v1 *decoration;
	struct wp_viewporter *viewporter;
	struct wl_subcompositor *subcompositor;
	struct wl_list seats; // nwl_seat
	struct wl_list outputs; // nwl_output
	struct wl_list surfaces; // nwl_surface
	struct xkb_context *keyboard_context;
	struct {
		EGLDisplay display;
		EGLConfig config;
		EGLContext context;
		cairo_device_t *cairo_dev;
		char inited;
	} egl;

	struct wl_cursor_theme *cursor_theme;
	uint32_t cursor_theme_size;
	uint32_t num_surfaces;
	char destroy_surfaces;
	struct nwl_poll *poll;
	bool use_pipewire;
};

struct nwl_output {
	struct wl_output *output;
	struct wl_list link;
	int scale;
};

char nwl_wayland_init(struct nwl_state *state);
void nwl_wayland_uninit(struct nwl_state *state);
void nwl_wayland_run(struct nwl_state *state);
void nwl_add_seat_fd(struct nwl_seat *seat);
// To be very inconsistent, this returns true on success..
bool nwl_egl_try_init(struct nwl_state *state);
void nwl_egl_uninit(struct nwl_state *state);

#endif

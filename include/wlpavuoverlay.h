#ifndef _WLPV_WLPAVUOVERLAY_H
#define _WLPV_WLPAVUOVERLAY_H
#define UNUSED(x) (void)(x)
#include <stdbool.h>
#include <EGL/egl.h>
#include <cairo/cairo.h>
#include <wayland-util.h>
#include "seat.h"
struct wlpavuo_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_decoration_manager_v1 *decoration;
	struct wp_viewporter *viewporter;
	struct wl_subcompositor *subcompositor;
	struct wl_list seats; // wlpavuo_seat
	struct wl_list outputs; // wlpavuo_output
	struct wl_list surfaces; // wlpavuo_surface
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
};

struct wlpavuo_output {
	struct wl_output *output;
	struct wl_list link;
	int scale;
};

char wlpavuo_wayland_init(struct wlpavuo_state *state);
void wlpavuo_wayland_uninit(struct wlpavuo_state *state);
void wlpavuo_wayland_run(struct wlpavuo_state *state);
char wlpavuoverlay_egl_init(struct wlpavuo_state *state);
void wlpavuoverlay_egl_uninit(struct wlpavuo_state *state);

#endif

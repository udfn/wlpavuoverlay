#ifndef _WLPV_SURFACE_H
#define _WLPV_SURFACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cairo.h>
#include <wayland-client.h>
#include "seat.h"
typedef void* EGLSurface;

struct wlpavuo_surface_shm {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
	int32_t stride;
	char *name;
	struct wl_buffer *buffer;
};

enum wlpavuo_surface_flags {
	WLPAVUO_SURFACE_FLAG_NEEDS_DRAW = 1 << 0,
	WLPAVUO_SURFACE_FLAG_CSD = 1 << 1,
	WLPAVUO_SURFACE_FLAG_DESTROY = 1 << 2,
	WLPAVUO_SURFACE_FLAG_NO_AUTOSCALE = 1 << 3,
};

// This is basically the xdg toplevel states..
enum wlpavuo_surface_states {
	WLPAVUO_SURFACE_STATE_ACTIVE = 1 << 0,
	WLPAVUO_SURFACE_STATE_MAXIMIZED = 1 << 1,
	WLPAVUO_SURFACE_STATE_FULLSCREEN = 1 << 2,
	WLPAVUO_SURFACE_STATE_RESIZING = 1 << 3,
	WLPAVUO_SURFACE_STATE_TILE_LEFT = 1 << 4,
	WLPAVUO_SURFACE_STATE_TILE_RIGHT = 1 << 5,
	WLPAVUO_SURFACE_STATE_TILE_TOP = 1 << 6,
	WLPAVUO_SURFACE_STATE_TILE_BOTTOM = 1 << 7,
};

struct wlpavuo_surface;
typedef char (*wlpavuo_surface_render_t)(struct wlpavuo_surface *surface);
typedef void (*wlpavuo_surface_destroy_t)(struct wlpavuo_surface *surface);
typedef void (*wlpavuo_surface_configure_t)(struct wlpavuo_surface *surface, uint32_t width, uint32_t height);
typedef void (*wlpavuo_surface_input_pointer_t)(struct wlpavuo_surface *surface, struct wlpavuo_pointer_event *event);
typedef void (*wlpavuo_surface_input_keyboard_t)(struct wlpavuo_surface *surface, struct wlpavuo_keyboard_event *event);

struct wlpavuo_surface {
	struct wl_list link; // link for wlpavuo_state
	struct wl_list sublink; // subsurface link
	struct wlpavuo_state *state;
	struct {
		struct wl_surface *surface;
		struct wl_subsurface *subsurface;
		struct zwlr_layer_surface_v1 *layer_surface;
		struct xdg_surface *xdg_surface;
		struct xdg_toplevel *xdg_toplevel;
		struct zxdg_toplevel_decoration_v1 *xdg_decoration;
		struct wp_viewport *viewport;
		struct wl_callback *frame_cb;
	} wl;
	struct {
		struct wl_egl_window *window;
		EGLSurface surface;
	} egl;
	cairo_surface_t *cairo_surface;
	struct wlpavuo_surface_shm *shm;
	uint32_t width, height;
	uint32_t desired_width, desired_height;
	uint32_t actual_width, actual_height;
	uint32_t scale;
	struct wlpavuo_surface *parent; // if a subsurface
	struct wl_list outputs; // wlpavuo_surf_outputs
	struct wl_list subsurfaces; // wlpavuo_surface
	enum wlpavuo_surface_flags flags;
	enum wlpavuo_surface_states states;
	char *title;
	uint32_t frame;
	struct {
		wlpavuo_surface_render_t render;
		wlpavuo_surface_destroy_t destroy;
		wlpavuo_surface_input_pointer_t input_pointer;
		wlpavuo_surface_input_keyboard_t input_keyboard;
		wlpavuo_surface_configure_t configure;
	} impl;
	void *userdata;
};

enum wlpavuo_surface_renderer {
	WLPAVUO_SURFACE_RENDER_SHM,
	WLPAVUO_SURFACE_RENDER_EGL
};

struct wlpavuo_surface *wlpavuo_surface_create(struct wlpavuo_state *state,char *title, enum wlpavuo_surface_renderer renderer);
void wlpavuo_surface_destroy(struct wlpavuo_surface *surface);
void wlpavuo_surface_destroy_later(struct wlpavuo_surface *surface);
void wlpavuo_surface_set_size(struct wlpavuo_surface *surface, uint32_t width, uint32_t height);
void wlpavuo_surface_apply_size(struct wlpavuo_surface *surface);
void wlpavuo_surface_swapbuffers(struct wlpavuo_surface *surface);
void wlpavuo_surface_set_need_draw(struct wlpavuo_surface *surface, bool rendernow);
void wlpavuo_surface_add_subsurface(struct wlpavuo_surface *parent, struct wlpavuo_surface *surface);

char wlpavuo_surface_role_layershell(struct wlpavuo_surface *surface, struct wl_output *output, uint32_t layer);
char wlpavuo_surface_role_toplevel(struct wlpavuo_surface *surface);

#endif

#ifndef _WLPV_SURFACE_H
#define _WLPV_SURFACE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cairo.h>
#include <wayland-client.h>
#include "seat.h"
typedef void* EGLSurface;

enum nwl_surface_flags {
	NWL_SURFACE_FLAG_NEEDS_DRAW = 1 << 0,
	NWL_SURFACE_FLAG_CSD = 1 << 1,
	NWL_SURFACE_FLAG_DESTROY = 1 << 2,
	NWL_SURFACE_FLAG_NO_AUTOSCALE = 1 << 3,
};

// This is basically the xdg toplevel states..
enum nwl_surface_states {
	NWL_SURFACE_STATE_ACTIVE = 1 << 0,
	NWL_SURFACE_STATE_MAXIMIZED = 1 << 1,
	NWL_SURFACE_STATE_FULLSCREEN = 1 << 2,
	NWL_SURFACE_STATE_RESIZING = 1 << 3,
	NWL_SURFACE_STATE_TILE_LEFT = 1 << 4,
	NWL_SURFACE_STATE_TILE_RIGHT = 1 << 5,
	NWL_SURFACE_STATE_TILE_TOP = 1 << 6,
	NWL_SURFACE_STATE_TILE_BOTTOM = 1 << 7,
};

struct nwl_surface;
typedef char (*nwl_surface_render_t)(struct nwl_surface *surface);
typedef void (*nwl_surface_destroy_t)(struct nwl_surface *surface);
typedef void (*nwl_surface_configure_t)(struct nwl_surface *surface, uint32_t width, uint32_t height);
typedef void (*nwl_surface_input_pointer_t)(struct nwl_surface *surface, struct nwl_pointer_event *event);
typedef void (*nwl_surface_input_keyboard_t)(struct nwl_surface *surface, struct nwl_keyboard_event *event);

typedef void (*nwl_surface_generic_func_t)(struct nwl_surface *surface);

struct nwl_surface {
	struct wl_list link; // link for nwl_state
	struct wl_list sublink; // subsurface link
	struct nwl_state *state;
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
		void *data;
		struct {
			nwl_surface_generic_func_t swapbuffers;
			nwl_surface_generic_func_t applysize;
			nwl_surface_generic_func_t destroy;
		} impl;
	} render;
	cairo_surface_t *cairo_surface;
	uint32_t width, height;
	uint32_t desired_width, desired_height;
	uint32_t actual_width, actual_height;
	uint32_t scale;
	struct nwl_surface *parent; // if a subsurface
	struct wl_list outputs; // nwl_surf_outputs
	struct wl_list subsurfaces; // nwl_surface
	enum nwl_surface_flags flags;
	enum nwl_surface_states states;
	char *title;
	uint32_t frame;
	struct {
		nwl_surface_render_t render;
		nwl_surface_destroy_t destroy;
		nwl_surface_input_pointer_t input_pointer;
		nwl_surface_input_keyboard_t input_keyboard;
		nwl_surface_configure_t configure;
	} impl;
	void *userdata;
};

enum nwl_surface_renderer {
	NWL_SURFACE_RENDER_SHM,
	NWL_SURFACE_RENDER_EGL
};

struct nwl_surface *nwl_surface_create(struct nwl_state *state,char *title, enum nwl_surface_renderer renderer);
void nwl_surface_destroy(struct nwl_surface *surface);
void nwl_surface_destroy_later(struct nwl_surface *surface);
bool nwl_surface_set_vp_destination(struct nwl_surface *surface, int32_t width, int32_t height);
void nwl_surface_set_size(struct nwl_surface *surface, uint32_t width, uint32_t height);
void nwl_surface_apply_size(struct nwl_surface *surface);
void nwl_surface_swapbuffers(struct nwl_surface *surface);
void nwl_surface_set_need_draw(struct nwl_surface *surface, bool rendernow);

void nwl_surface_role_subsurface(struct nwl_surface *parent, struct nwl_surface *surface);
char nwl_surface_role_layershell(struct nwl_surface *surface, struct wl_output *output, uint32_t layer);
char nwl_surface_role_toplevel(struct nwl_surface *surface);

#endif

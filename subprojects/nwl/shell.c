#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-decoration-unstable-v1.h"
#include "xdg-shell.h"
#include "viewporter.h"
#include "nwl/nwl.h"
#include "nwl/surface.h"

static void handle_layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial, uint32_t width, uint32_t height ) {
	struct nwl_surface *surf = (struct nwl_surface*)data;
	zwlr_layer_surface_v1_ack_configure(layer, serial);
	if (surf->impl.configure) {
		surf->impl.configure(surf,width,height);
		nwl_surface_apply_size(surf);
		wl_surface_commit(surf->wl.surface);
		return;
	} else if (surf->width != width || surf->height != height) {
		surf->width = width;
		surf->height = height;
	}
	nwl_surface_apply_size(surf);
	wl_surface_commit(surf->wl.surface);
}

static void handle_layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) {
	UNUSED(layer);
	struct nwl_surface *surf = data;
	nwl_surface_destroy_later(surf);
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {
	handle_layer_configure,
	handle_layer_closed
};

static void handle_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct nwl_surface *surf = data;
	xdg_surface_ack_configure(xdg_surface, serial);
	// Ugly hack!
	xdg_surface_set_window_geometry(xdg_surface, 0,0, surf->width, surf->height);
	nwl_surface_apply_size(surf);
}

static const struct xdg_surface_listener surface_listener = {
	handle_surface_configure
};

static void handle_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	UNUSED(xdg_toplevel);
	struct nwl_surface *surf = data;
	uint32_t *state = 0;
	enum nwl_surface_states newstates = 0;
	wl_array_for_each(state, states) {
		switch (*state) {
			case XDG_TOPLEVEL_STATE_MAXIMIZED:
				newstates |= NWL_SURFACE_STATE_MAXIMIZED;
				break;
			case XDG_TOPLEVEL_STATE_ACTIVATED:
				newstates |= NWL_SURFACE_STATE_ACTIVE;
				break;
			case XDG_TOPLEVEL_STATE_RESIZING:
				newstates |= NWL_SURFACE_STATE_RESIZING;
				break;
			case XDG_TOPLEVEL_STATE_FULLSCREEN:
				newstates |= NWL_SURFACE_STATE_FULLSCREEN;
				break;
			case XDG_TOPLEVEL_STATE_TILED_LEFT:
				newstates |= NWL_SURFACE_STATE_TILE_LEFT;
				break;
			case XDG_TOPLEVEL_STATE_TILED_RIGHT:
				newstates |= NWL_SURFACE_STATE_TILE_RIGHT;
				break;
			case XDG_TOPLEVEL_STATE_TILED_TOP:
				newstates |= NWL_SURFACE_STATE_TILE_TOP;
				break;
			case XDG_TOPLEVEL_STATE_TILED_BOTTOM:
				newstates |= NWL_SURFACE_STATE_TILE_BOTTOM;
				break;
		}
	}
	surf->states = newstates;
	if (surf->impl.configure) {
		surf->impl.configure(surf, width, height);
		return;
	}
	if (width == 0) {
		width = surf->desired_width;
	}
	if (height == 0) {
		height = surf->desired_height;
	}
	surf->width = width;
	surf->height = height;
}

static void handle_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
	UNUSED(xdg_toplevel);
	struct nwl_surface *surf = data;
	nwl_surface_destroy_later(surf);
}

static const struct xdg_toplevel_listener toplevel_listener = {
	handle_toplevel_configure,
	handle_toplevel_close
};

static void handle_decoration_configure(
		void *data,
		struct zxdg_toplevel_decoration_v1 *zxdg_toplevel_decoration_v1,
		uint32_t mode) {
	UNUSED(zxdg_toplevel_decoration_v1);
	struct nwl_surface *surf = (struct nwl_surface*)data;
	if (mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
		surf->flags |= NWL_SURFACE_FLAG_CSD;
	} else {
		surf->flags = surf->flags & ~NWL_SURFACE_FLAG_CSD;
	}
}
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	handle_decoration_configure
};


char nwl_surface_role_layershell(struct nwl_surface *surface, struct wl_output *output, uint32_t layer) {
	if (!surface->state->layer_shell) {
		return 1;
	}
	surface->wl.layer_surface = zwlr_layer_shell_v1_get_layer_surface(surface->state->layer_shell, surface->wl.surface, output,
			layer, surface->title);
	zwlr_layer_surface_v1_add_listener(surface->wl.layer_surface, &layer_listener, surface);
	return 0;
}

char nwl_surface_role_toplevel(struct nwl_surface *surface) {
	if (!surface->state->xdg_wm_base) {
		return 1;
	}
	surface->wl.xdg_surface = xdg_wm_base_get_xdg_surface(surface->state->xdg_wm_base, surface->wl.surface);
	surface->wl.xdg_toplevel = xdg_surface_get_toplevel(surface->wl.xdg_surface);
	xdg_toplevel_add_listener(surface->wl.xdg_toplevel, &toplevel_listener, surface);
	xdg_surface_add_listener(surface->wl.xdg_surface, &surface_listener, surface);
	xdg_toplevel_set_title(surface->wl.xdg_toplevel, surface->title);
	if (surface->state->decoration) {
		surface->wl.xdg_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(surface->state->decoration, surface->wl.xdg_toplevel);
		zxdg_toplevel_decoration_v1_add_listener(surface->wl.xdg_decoration, &decoration_listener, surface);
	}
	return 0;
}

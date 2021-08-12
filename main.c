#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <wayland-cursor.h>
#include <GL/gl.h>
#include <cairo/cairo-gl.h>
#include <cairo/cairo.h>
#include <nwl/nwl.h>
#include <nwl/surface.h>
#include <nwl/cairo.h>
#include "ui.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-decoration-unstable-v1.h"
#include "viewporter.h"

static bool surface_render(struct nwl_surface *surf, cairo_surface_t *cairo_surface) {
	cairo_t *cr = cairo_create(cairo_surface);
	char ret = wlpavuo_ui_run(surf, cr);
	if (surf->flags & NWL_SURFACE_FLAG_DESTROY) {
		ret = false;
	}
	cairo_destroy(cr);
	return ret;
}

struct bgstatus_t {
	char bgrendered;
	uint32_t actual_width;
	uint32_t actual_height;
	struct nwl_surface *main_surface;
};

static bool background_surface_render(struct nwl_surface *surf, cairo_surface_t *cairo_surface) {
	char ret = 0;
	bool bgdrawn = false;
	struct bgstatus_t *bgstatus = surf->userdata;
	if (!bgstatus->bgrendered) {
		cairo_t *cr = cairo_create(cairo_surface);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_rectangle(cr, 0,0,surf->width,surf->height);
		cairo_set_source_rgba(cr, 0., 0,0, 0.45);
		cairo_fill(cr);
		cairo_destroy(cr);
		bgstatus->bgrendered = 1;
		bgdrawn = true;
	}
	struct nwl_surface *subsurf;
	wl_list_for_each(subsurf, &surf->subsurfaces, sublink) {
		if (subsurf->renderer.impl->render(subsurf)) {
			nwl_surface_swapbuffers(subsurf);
			ret = 1;
		}
		if (bgstatus->actual_height != surf->actual_height || bgstatus->actual_width != surf->actual_width) {
			bgstatus->actual_width = surf->actual_width;
			bgstatus->actual_height = surf->actual_height;
			wl_subsurface_set_position(subsurf->wl.subsurface, (surf->actual_width/2)-(subsurf->width/2),
				(surf->actual_height/2)-(subsurf->height/2));
		}
	}
	if (ret) {
		wl_surface_commit(surf->wl.surface);
	}
	return bgdrawn;
}

static void background_surface_input_keyboard(struct nwl_surface *surf, struct nwl_keyboard_event *event) {
	struct bgstatus_t *bgstatus = surf->userdata;
	bgstatus->main_surface->impl.input_keyboard(bgstatus->main_surface,event);
}

static void background_surface_input_pointer(struct nwl_surface *surf, struct nwl_pointer_event *event) {
	if (event->changed & NWL_POINTER_EVENT_BUTTON && event->buttons & NWL_MOUSE_LEFT) {
		nwl_surface_destroy_later(surf);
	}
}

static void background_surface_configure(struct nwl_surface *surf, uint32_t width, uint32_t height) {
	if (surf->width != width || surf->height != height) {
		if (nwl_surface_set_vp_destination(surf,width,height)) {
			surf->width = 1;
			surf->height = 1;
			return;
		}
		// No viewport? Oh well..
		surf->width = width;
		surf->height = height;
		surf->actual_height = height;
		surf->actual_width = width;
	}
}

static void background_surface_destroy(struct nwl_surface *surf) {
	free(surf->userdata);
}

static bool set_surface_role(struct nwl_surface *surface, char layer) {
	if (!layer || !nwl_surface_role_layershell(surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)) {
		if (!nwl_surface_role_toplevel(surface)) {
			fprintf(stderr, "Unable to set surface role, is your compositor broken?\n");
			return false;
		}
	}
	else {
		zwlr_layer_surface_v1_set_keyboard_interactivity(surface->wl.layer_surface, 1);
		surface->states |= NWL_SURFACE_STATE_ACTIVE;
	}
	return true;
}

enum wlpavuo_surface_layer_mode {
	WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL,
};

static void setup_wlpavuo_ui_surface(struct nwl_surface *surf) {
	nwl_surface_renderer_cairo(surf, surface_render);
	surf->impl.destroy = wlpavuo_ui_destroy;
	surf->impl.input_pointer = wlpavuo_ui_input_pointer;
	surf->impl.input_keyboard = wlpavuo_ui_input_keyboard;
}

static void create_surface(struct nwl_state *state, enum wlpavuo_surface_layer_mode layer) {
	struct wlpavuo_state *wlpstate = state->userdata;
	enum nwl_surface_renderer renderer = wlpstate->use_shm ? NWL_SURFACE_RENDER_SHM : NWL_SURFACE_RENDER_EGL;
	struct nwl_surface *surf = nwl_surface_create(state,"WlPaVUOverlay", renderer);
	if (!set_surface_role(surf, layer != WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL)) {
		nwl_surface_destroy(surf);
		return;
	}
	if (surf->wl.layer_surface) {
		if (layer == WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL) {
			setup_wlpavuo_ui_surface(surf);
			nwl_surface_set_size(surf, 520, 540);
			//surf->flags = surf->flags & ~WLPAVUO_SURFACE_FLAG_CSD;
		} else {
			surf->userdata = calloc(1,sizeof(struct bgstatus_t));
			surf->impl.destroy = background_surface_destroy;
			surf->impl.input_pointer = background_surface_input_pointer;
			surf->impl.input_keyboard = background_surface_input_keyboard;
			surf->impl.configure = background_surface_configure;
			nwl_surface_renderer_cairo(surf, background_surface_render);
			struct bgstatus_t *bgs = surf->userdata;
			zwlr_layer_surface_v1_set_anchor(surf->wl.layer_surface,
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
			zwlr_layer_surface_v1_set_exclusive_zone(surf->wl.layer_surface, -1);
			nwl_surface_set_size(surf, 0,0);
			if (state->viewporter) {
				surf->wl.viewport = wp_viewporter_get_viewport(state->viewporter, surf->wl.surface);
			}
			struct nwl_surface *subsurf = nwl_surface_create(state, "WlPaVUOverlay sub", renderer);
			nwl_surface_role_subsurface(subsurf, surf);
			wl_subsurface_set_position(subsurf->wl.subsurface, 0,0);
			nwl_surface_set_size(subsurf, 540, 540);
			subsurf->states |= NWL_SURFACE_STATE_ACTIVE;
			subsurf->flags = subsurf->flags & ~NWL_SURFACE_FLAG_CSD;
			bgs->main_surface = subsurf;
			setup_wlpavuo_ui_surface(subsurf);
			wl_surface_commit(subsurf->wl.surface);
		}
	} else {
		setup_wlpavuo_ui_surface(surf);
		nwl_surface_set_size(surf, 520, 540);
	}
	wl_surface_commit(surf->wl.surface);
}

int main (int argc, char *argv[]) {
	struct nwl_state state = {0};
	state.xdg_app_id = "wlpavuoverlay";
	struct wlpavuo_state wlpstate = {0};
	if (nwl_wayland_init(&state)) {
		fprintf(stderr,"failed to init, bailing!\n");
		return 1;
	}
	state.userdata = &wlpstate;
	if (state.compositor) {
		char mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL;
		for (int i = 1; i < argc;i++) {
			if (strcmp(argv[i], "dim") == 0) {
				mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL;
			} else if (strcmp(argv[i], "xdg") == 0) {
				mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
			} else if (strcmp(argv[i], "pw") == 0) {
				wlpstate.use_pipewire = true;
			} else if (strcmp(argv[i], "shm") == 0) {
				wlpstate.use_shm = true;
			}
		}
		create_surface(&state, mode);
	}
	nwl_wayland_run(&state);
	nwl_wayland_uninit(&state);
	return 0;
}

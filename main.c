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
#include "viewporter.h"

enum wlpavuo_surface_layer_mode {
	WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL,
};

static void surface_render(struct nwl_surface *surf, cairo_surface_t *cairo_surface) {
	cairo_t *cr = cairo_create(cairo_surface);
	char ret = wlpavuo_ui_run(surf, cr);
	if (surf->states & NWL_SURFACE_STATE_DESTROY) {
		ret = false;
	}
	cairo_destroy(cr);
	if (ret) {
		nwl_surface_swapbuffers(surf, 0, 0);
		if (surf->parent) {
			wl_surface_damage_buffer(surf->parent->wl.surface, 0, 0, surf->parent->width, surf->parent->height);
			wl_surface_commit(surf->parent->wl.surface);
		}
	}
}

struct bgstatus_t {
	char bgrendered;
	uint32_t actual_width;
	uint32_t actual_height;
	struct nwl_surface *main_surface;
};

static void background_surface_render(struct nwl_surface *surf, cairo_surface_t *cairo_surface) {
	struct bgstatus_t *bgstatus = surf->userdata;
	struct nwl_surface *subsurf = bgstatus->main_surface;
	if (bgstatus->actual_height != surf->desired_height ||
			bgstatus->actual_width != surf->desired_height) {
		bgstatus->actual_width = surf->desired_width;
		bgstatus->actual_height = surf->desired_height;
		wl_subsurface_set_position(subsurf->role.subsurface.wl, (surf->desired_width/2)-(subsurf->width/2),
			(surf->desired_height/2)-(subsurf->height/2));
		wl_surface_commit(subsurf->wl.surface);
	}
	if (bgstatus->main_surface->scale != surf->scale) {
		// Hack: subsurface is not getting output enter events
		bgstatus->main_surface->scale = surf->scale;
		bgstatus->main_surface->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		// Also have to rerender the background for things to work
		bgstatus->bgrendered = false;
	}
	if (!bgstatus->bgrendered) {
		cairo_t *cr = cairo_create(cairo_surface);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
		cairo_paint(cr);
		cairo_destroy(cr);
		bgstatus->bgrendered = 1;
		nwl_surface_set_need_draw(bgstatus->main_surface, true);
		nwl_surface_swapbuffers(surf, 0, 0);
		return;
	}
	wl_surface_commit(surf->wl.surface);
}

static void background_surface_input_keyboard(struct nwl_surface *surf, struct nwl_seat *seat, struct nwl_keyboard_event *event) {
	struct bgstatus_t *bgstatus = surf->userdata;
	bgstatus->main_surface->impl.input_keyboard(bgstatus->main_surface, seat, event);
}

static void background_surface_input_pointer(struct nwl_surface *surf, struct nwl_seat *seat, struct nwl_pointer_event *event) {
	UNUSED(seat);
	if (event->changed & NWL_POINTER_EVENT_BUTTON && event->buttons & NWL_MOUSE_LEFT) {
		nwl_surface_destroy_later(surf);
	}
}

static void background_surface_configure(struct nwl_surface *surf, uint32_t width, uint32_t height) {
	if (surf->width != width || surf->height != height) {
		if (nwl_surface_set_vp_destination(surf, width, height)) {
			// Er, abusing desired size for this? Ok then!
			surf->desired_width = width;
			surf->desired_height = height;
			surf->width = 1;
			surf->height = 1;
			return;
		}
		// No viewport? Oh well..
		surf->width = width;
		surf->height = height;
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
		zwlr_layer_surface_v1_set_keyboard_interactivity(surface->role.layer.wl, 1);
		surface->states |= NWL_SURFACE_STATE_ACTIVE;
	}
	return true;
}

static void setup_wlpavuo_ui_surface(struct wlpavuo_state *wlpstate, struct nwl_surface *surf) {
	nwl_surface_renderer_cairo(surf, !wlpstate->use_shm, surface_render);
	surf->impl.destroy = wlpavuo_ui_destroy;
	surf->impl.input_pointer = wlpavuo_ui_input_pointer;
	surf->impl.input_keyboard = wlpavuo_ui_input_keyboard;
}

static void create_surface(struct nwl_state *state, enum wlpavuo_surface_layer_mode layer) {
	struct wlpavuo_state *wlpstate = state->userdata;
	struct nwl_surface *surf = nwl_surface_create(state, "WlPaVUOverlay");
	if (!set_surface_role(surf, layer != WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL)) {
		nwl_surface_destroy(surf);
		return;
	}
	if (surf->role_id == NWL_SURFACE_ROLE_LAYER) {
		if (layer == WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL) {
			setup_wlpavuo_ui_surface(wlpstate, surf);
			nwl_surface_set_size(surf, wlpstate->width, wlpstate->height);
			surf->states |= NWL_SURFACE_STATE_CSD;
		} else {
			surf->userdata = calloc(1, sizeof(struct bgstatus_t));
			surf->impl.destroy = background_surface_destroy;
			surf->impl.input_pointer = background_surface_input_pointer;
			surf->impl.input_keyboard = background_surface_input_keyboard;
			surf->impl.configure = background_surface_configure;
			nwl_surface_renderer_cairo(surf, !wlpstate->use_shm, background_surface_render);
			struct bgstatus_t *bgs = surf->userdata;
			zwlr_layer_surface_v1_set_anchor(surf->role.layer.wl,
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
			zwlr_layer_surface_v1_set_exclusive_zone(surf->role.layer.wl, -1);
			nwl_surface_set_size(surf, 0, 0);
			if (state->wl.viewporter) {
				surf->wl.viewport = wp_viewporter_get_viewport(state->wl.viewporter, surf->wl.surface);
			}
			struct nwl_surface *subsurf = nwl_surface_create(state, "WlPaVUOverlay sub");
			setup_wlpavuo_ui_surface(wlpstate, subsurf);
			nwl_surface_role_subsurface(subsurf, surf);
			nwl_surface_set_size(subsurf, wlpstate->width, wlpstate->height);
			subsurf->states |= NWL_SURFACE_STATE_ACTIVE;
			bgs->main_surface = subsurf;
		}
	} else {
		setup_wlpavuo_ui_surface(wlpstate, surf);
		nwl_surface_set_size(surf, wlpstate->width, wlpstate->height);
	}
	wl_surface_commit(surf->wl.surface);
}

int main (int argc, char *argv[]) {
	struct nwl_state state = {0};
	struct wlpavuo_state wlpstate = {
		.width = 620,
		.height = 640,
		.mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL
	};
	int opt;
	while ((opt = getopt(argc, argv, "dxpsw:h:")) != -1) {
		switch (opt) {
			case 'd':
				wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL;
				break;
			case 'x':
				wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
				break;
			case 'p': wlpstate.use_pipewire = true; break;
			case 's': wlpstate.use_shm = true; break;
			case 'w':;
				int width = atoi(optarg);
				if (width > 0)
					wlpstate.width = width;
				break;
			case 'h':;
				int height = atoi(optarg);
				if (height > 0)
					wlpstate.height = height;
				break;
		default:
			break;
		}
	}

	for (int i = optind; i < argc; i++) {
		if (strcmp(argv[i], "dim") == 0) {
			wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL;
		} else if (strcmp(argv[i], "xdg") == 0) {
			wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
		} else if (strcmp(argv[i], "pw") == 0) {
			wlpstate.use_pipewire = true;
		} else if (strcmp(argv[i], "shm") == 0) {
			wlpstate.use_shm = true;
		}
	}
	state.xdg_app_id = "wlpavuoverlay";
	if (nwl_wayland_init(&state)) {
		fprintf(stderr, "failed to init, bailing!\n");
		return 1;
	}
	state.userdata = &wlpstate;
	if (state.wl.compositor) {
		create_surface(&state, wlpstate.mode);
	}
	nwl_wayland_run(&state);
	nwl_wayland_uninit(&state);
	return 0;
}

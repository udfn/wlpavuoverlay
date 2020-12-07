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
#include "wlpavuoverlay.h"
#include "ui.h"
#include "surface.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-decoration-unstable-v1.h"
#include "viewporter.h"

static char surface_render(struct wlpavuo_surface *surf) {
	cairo_t *cr = cairo_create(surf->cairo_surface);
	char ret = wlpavuo_ui_run(surf, cr);
	if (ret && !(surf->flags & WLPAVUO_SURFACE_FLAG_DESTROY)) {
		wlpavuo_surface_swapbuffers(surf);
	}
	cairo_destroy(cr);
	return ret;
}

struct bgstatus_t {
	char bgrendered;
	uint32_t actual_width;
	uint32_t actual_height;
	struct wlpavuo_surface *main_surface;
};

static char background_surface_render(struct wlpavuo_surface *surf) {
	char ret = 0;
	struct bgstatus_t *bgstatus = surf->userdata;
	if (!bgstatus->bgrendered) {
		cairo_t *cr = cairo_create(surf->cairo_surface);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_rectangle(cr, 0,0,surf->width,surf->height);
		cairo_set_source_rgba(cr, 0., 0,0, 0.45);
		cairo_fill(cr);
		cairo_destroy(cr);
		wlpavuo_surface_swapbuffers(surf);
		bgstatus->bgrendered = 1;
		ret = 1;
	}
	struct wlpavuo_surface *subsurf;
	wl_list_for_each(subsurf, &surf->subsurfaces, sublink) {
		if (subsurf->impl.render(subsurf)) {
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
	return ret;
}

static void background_surface_input_keyboard(struct wlpavuo_surface *surf, struct wlpavuo_keyboard_event *event) {
	struct bgstatus_t *bgstatus = surf->userdata;
	bgstatus->main_surface->impl.input_keyboard(bgstatus->main_surface,event);
}

static void background_surface_input_pointer(struct wlpavuo_surface *surf, struct wlpavuo_pointer_event *event) {
	if (event->changed & WLPAVUO_POINTER_EVENT_BUTTON && event->buttons & WLPAVUO_MOUSE_LEFT) {
		wlpavuo_surface_destroy_later(surf);
	}
}

static void background_surface_configure(struct wlpavuo_surface *surf, uint32_t width, uint32_t height) {
	if (surf->width != width || surf->height != height) {
		if (surf->wl.viewport) {
			surf->width = 1;
			surf->height = 1;
			wp_viewport_set_destination(surf->wl.viewport, width, height);
		} else {
			// No viewport? Oh well..
			surf->width = width;
			surf->height = height;
		}
		surf->actual_height = height;
		surf->actual_width = width;
	}
}

static void background_surface_destroy(struct wlpavuo_surface *surf) {
	free(surf->userdata);
}

static void set_surface_role(struct wlpavuo_surface *surface, char layer) {
	if (!layer || wlpavuo_surface_role_layershell(surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY)) {
		if (wlpavuo_surface_role_toplevel(surface)) {
			return;
		}
	}
	else {
		zwlr_layer_surface_v1_set_keyboard_interactivity(surface->wl.layer_surface, 1);
		surface->states |= WLPAVUO_SURFACE_STATE_ACTIVE;
	}
}

enum wlpavuo_surface_layer_mode {
	WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL,
};

static void setup_wlpavuo_ui_surface(struct wlpavuo_surface *surf) {
	surf->impl.render = surface_render;
	surf->impl.destroy = wlpavuo_ui_destroy;
	surf->impl.input_pointer = wlpavuo_ui_input_pointer;
	surf->impl.input_keyboard = wlpavuo_ui_input_keyboard;
}

static void create_surface(struct wlpavuo_state *state, enum wlpavuo_surface_layer_mode layer) {
	struct wlpavuo_surface *surf = wlpavuo_surface_create(state,"WlPaVUOverlay", WLPAVUO_SURFACE_RENDER_EGL);
	set_surface_role(surf, layer != WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL);
	if (surf->wl.layer_surface) {
		if (layer == WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL) {
			setup_wlpavuo_ui_surface(surf);
			wlpavuo_surface_set_size(surf, 520, 540);
			//surf->flags = surf->flags & ~WLPAVUO_SURFACE_FLAG_CSD;
		} else {
			surf->userdata = calloc(1,sizeof(struct bgstatus_t));
			surf->impl.render = background_surface_render;
			surf->impl.destroy = background_surface_destroy;
			surf->impl.input_pointer = background_surface_input_pointer;
			surf->impl.input_keyboard = background_surface_input_keyboard;
			surf->impl.configure = background_surface_configure;
			struct bgstatus_t *bgs = surf->userdata;
			zwlr_layer_surface_v1_set_anchor(surf->wl.layer_surface,
				ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|
				ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
			zwlr_layer_surface_v1_set_exclusive_zone(surf->wl.layer_surface, -1);
			wlpavuo_surface_set_size(surf, 0,0);
			if (state->viewporter) {
				surf->wl.viewport = wp_viewporter_get_viewport(state->viewporter, surf->wl.surface);
			}
			struct wlpavuo_surface *subsurf = wlpavuo_surface_create(state, "WlPaVUOverlay sub", WLPAVUO_SURFACE_RENDER_EGL);
			wlpavuo_surface_add_subsurface(surf, subsurf);
			wl_subsurface_set_position(subsurf->wl.subsurface, 0,0);
			wlpavuo_surface_set_size(subsurf, 540, 540);
			subsurf->states |= WLPAVUO_SURFACE_STATE_ACTIVE;
			subsurf->flags = subsurf->flags & ~WLPAVUO_SURFACE_FLAG_CSD;
			bgs->main_surface = subsurf;
			wlpavuo_surface_apply_size(subsurf);
			setup_wlpavuo_ui_surface(subsurf);
			wl_surface_commit(subsurf->wl.surface);
		}
	} else {
		setup_wlpavuo_ui_surface(surf);
		wlpavuo_surface_set_size(surf, 520, 540);
	}
	wl_surface_commit(surf->wl.surface);
}

int main (int argc, char *argv[]) {
	struct wlpavuo_state state = {0};
	if (wlpavuo_wayland_init(&state)) {
		fprintf(stderr,"failed to init, bailing!\n");
		return 1;
	}
	if (state.compositor) {
		char mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL;
		for (int i = 1; i < argc;i++) {
			if (strcmp(argv[i], "dim") == 0) {
				mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL;
			} else if (strcmp(argv[i], "xdg") == 0) {
				mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
			} else if (strcmp(argv[i], "pw") == 0) {
				state.use_pipewire = true;
			}
		}
		create_surface(&state, mode);
	}
	wlpavuo_wayland_run(&state);
	wlpavuo_wayland_uninit(&state);
	return 0;
}

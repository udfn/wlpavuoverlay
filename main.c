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
#include <cairo/cairo.h>
#include <nwl/nwl.h>
#include <nwl/surface.h>
#include <nwl/seat.h>
#include <nwl/cairo.h>
#include "ui.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "single-pixel-buffer-v1.h"
#include "xdg-shell.h"
#include "viewporter.h"

enum wlpavuo_surface_layer_mode {
	WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL,
	WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL,
};

static void surface_render(struct nwl_surface *surf, struct nwl_cairo_surface *cairo_surface) {
	cairo_t *cr = cairo_surface->ctx;
	cairo_identity_matrix(cr);
	char ret = wlpavuo_ui_run(surf, cr);
	if (surf->states & NWL_SURFACE_STATE_DESTROY) {
		ret = false;
	}
	if (ret) {
		nwl_surface_swapbuffers(surf, 0, 0);
		if (surf->role_id == NWL_SURFACE_ROLE_SUB) {
			struct nwl_surface *parent = surf->role.subsurface.parent;
			wl_surface_damage_buffer(parent->wl.surface, 0, 0, parent->width, parent->height);
			wl_surface_commit(parent->wl.surface);
		}
	}
}

static void background_surface_update_sub(struct wlpavuo_surface *bgstatus) {
	struct nwl_surface *subsurf = &bgstatus->main_surface;
	struct wlpavuo_state *wlpstate = wl_container_of(bgstatus->bg_surface.state, wlpstate, nwl);
	if (bgstatus->actual_height != bgstatus->bg_surface.desired_height ||
			bgstatus->actual_width != bgstatus->bg_surface.desired_height) {
		bgstatus->actual_width = bgstatus->bg_surface.desired_width;
		bgstatus->actual_height = bgstatus->bg_surface.desired_height;
		wl_subsurface_set_position(subsurf->role.subsurface.wl, (bgstatus->bg_surface.desired_width/2)-(subsurf->width/2),
			(bgstatus->bg_surface.desired_height/2)-(wlpstate->height/2));
		wl_surface_commit(subsurf->wl.surface);
	}
	if (subsurf->scale != bgstatus->bg_surface.scale) {
		// Hack: subsurface is not getting output enter events
		subsurf->scale = bgstatus->bg_surface.scale;
		subsurf->states |= NWL_SURFACE_STATE_NEEDS_APPLY_SIZE;
		// Also have to rerender the background for things to work
		bgstatus->bgrendered = false;
	}
}

static void background_surface_render(struct nwl_surface *surf, struct nwl_cairo_surface *cairo_surface) {
	struct wlpavuo_surface *bgstatus = wl_container_of(surf, bgstatus, bg_surface);
	background_surface_update_sub(bgstatus);
	if (!bgstatus->bgrendered) {
		cairo_t *cr = cairo_surface->ctx;
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0, 0, 0, 0.45);
		cairo_paint(cr);
		bgstatus->bgrendered = 1;
		nwl_surface_set_need_draw(&bgstatus->main_surface, true);
		nwl_surface_swapbuffers(surf, 0, 0);
		return;
	}
	wl_surface_commit(surf->wl.surface);
}

static void background_surface_render_sp(struct nwl_surface *surf) {
	struct wlpavuo_surface *bgstatus = wl_container_of(surf, bgstatus, bg_surface);
	background_surface_update_sub(bgstatus);
	if (!bgstatus->bgrendered) {
		bgstatus->bgrendered = true;
		nwl_surface_swapbuffers(surf, 0, 0);
		nwl_surface_set_need_draw(&bgstatus->main_surface, true);
	}
	wl_surface_commit(surf->wl.surface);
}

static void sp_renderer_noop(struct nwl_surface *surface) {
	return;
}

static void sp_renderer_destroy(struct nwl_surface *surface) {
	if (surface->render.data) {
		wl_buffer_destroy(surface->render.data);
	}
}

static void sp_renderer_swapbuffers(struct nwl_surface *surface, int32_t x, int32_t y) {
	struct wlpavuo_state *wlpstate = wl_container_of(surface->state, wlpstate, nwl);
	if (surface->render.data) {
		return;
	}
	surface->render.data = wp_single_pixel_buffer_manager_v1_create_u32_rgba_buffer(wlpstate->sp_buffer_manager, 0, 0, 0, 1932735282);
	wl_surface_attach(surface->wl.surface, surface->render.data, 0, 0);
}


struct nwl_renderer_impl background_surface_sp_renderer_impl = {
	.swap_buffers = sp_renderer_swapbuffers,
	.apply_size = sp_renderer_noop,
	.render = background_surface_render_sp,
	.destroy = sp_renderer_destroy
};

static void background_surface_input_keyboard(struct nwl_surface *surf, struct nwl_seat *seat, struct nwl_keyboard_event *event) {
	struct wlpavuo_surface *bgstatus = wl_container_of(surf, bgstatus, bg_surface);
	bgstatus->main_surface.impl.input_keyboard(&bgstatus->main_surface, seat, event);
}

static void background_surface_input_pointer(struct nwl_surface *surf, struct nwl_seat *seat, struct nwl_pointer_event *event) {
	UNUSED(seat);
	if (event->changed & NWL_POINTER_EVENT_BUTTON && event->buttons & NWL_MOUSE_LEFT) {
		nwl_surface_destroy_later(surf);
	}
}

static void background_surface_configure(struct nwl_surface *surf, uint32_t width, uint32_t height) {
	if (surf->width != width || surf->height != height) {
		struct wlpavuo_surface *bgs = wl_container_of(surf, bgs, bg_surface);
		if (bgs->viewport) {
			wp_viewport_set_destination(bgs->viewport, width, height);
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
	struct wlpavuo_surface *bgs = wl_container_of(surf, bgs, bg_surface);
	if (bgs->viewport) {
		wp_viewport_destroy(bgs->viewport);
	}
}

static void setup_wlpavuo_ui_surface(struct wlpavuo_state *wlpstate) {
	struct nwl_surface *surf = &wlpstate->surface.main_surface;
	nwl_surface_renderer_cairo(surf, surface_render, 0);
	surf->impl.destroy = wlpavuo_ui_destroy;
	if (!wlpstate->no_seat) {
		surf->impl.input_pointer = wlpavuo_ui_input_pointer;
		surf->impl.input_keyboard = wlpavuo_ui_input_keyboard;
	}
	if (wlpstate->stdin_input) {
		nwl_poll_add_fd(surf->state, 0, wlpavuo_ui_input_stdin, &wlpstate->surface);
	}
}

static void create_surface(struct wlpavuo_state *wlpstate) {
	struct wlpavuo_surface *surf = &wlpstate->surface;
	nwl_surface_init(&surf->main_surface, &wlpstate->nwl, "WlPaVUOverlay");
	setup_wlpavuo_ui_surface(wlpstate);
	switch (wlpstate->mode) {
		case WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL:
		nwl_surface_init(&surf->bg_surface, &wlpstate->nwl, "WlPaVUOverlay");
		nwl_surface_role_layershell(&surf->bg_surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
		nwl_surface_role_subsurface(&surf->main_surface, &surf->bg_surface);
		surf->bg_surface.impl.destroy = background_surface_destroy;
		surf->main_surface.states |= NWL_SURFACE_STATE_ACTIVE;
		if (!wlpstate->no_seat) {
			surf->bg_surface.impl.input_pointer = background_surface_input_pointer;
			surf->bg_surface.impl.input_keyboard = background_surface_input_keyboard;
		}
		surf->bg_surface.impl.configure = background_surface_configure;
		if (wlpstate->sp_buffer_manager) {
			surf->bg_surface.render.impl = &background_surface_sp_renderer_impl;
			surf->bg_surface.render.data = NULL;
		} else {
			nwl_surface_renderer_cairo(&surf->bg_surface, background_surface_render, 0);
		}
		zwlr_layer_surface_v1_set_anchor(surf->bg_surface.role.layer.wl,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM|ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP|
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT|ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_exclusive_zone(surf->bg_surface.role.layer.wl, -1);
		nwl_surface_set_size(&surf->bg_surface, 0, 0);
		if (wlpstate->viewporter) {
			surf->viewport = wp_viewporter_get_viewport(wlpstate->viewporter, surf->bg_surface.wl.surface);
		}
		break;
		case WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL:
		nwl_surface_role_layershell(&surf->main_surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
		zwlr_layer_surface_v1_set_anchor(surf->main_surface.role.layer.wl, wlpstate->anchor);
		surf->main_surface.states |= NWL_SURFACE_STATE_CSD;
		break;
		case WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL:
		nwl_surface_role_toplevel(&surf->main_surface);
		nwl_surface_set_size(&surf->main_surface, wlpstate->width, wlpstate->height);
		break;
	}
	if (surf->main_surface.role_id != NWL_SURFACE_ROLE_TOPLEVEL) {
		uint32_t base_height = 140;
		if (!wlpstate->no_seat) {
			zwlr_layer_surface_v1_set_keyboard_interactivity(wlpstate->mode == WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL ? 
				surf->bg_surface.role.layer.wl : surf->main_surface.role.layer.wl, 1);
			base_height += 40;
		} else {
			struct wl_region *region = wl_compositor_create_region(wlpstate->nwl.wl.compositor);
			wl_surface_set_input_region(surf->main_surface.wl.surface, region);
			wl_region_destroy(region);
			surf->main_surface.states &= ~NWL_SURFACE_STATE_CSD;
		}
		nwl_surface_set_size(&surf->main_surface, wlpstate->width, wlpstate->dynamic_height ? base_height : wlpstate->height);
	}
	wl_surface_commit(surf->main_surface.wl.surface);
	if (wlpstate->mode == WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL) {
		wl_surface_commit(surf->bg_surface.wl.surface);
	}
}

bool handle_global(struct nwl_state *state, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct wlpavuo_state *wlpstate = wl_container_of(state, wlpstate, nwl);
	if (strcmp(wp_single_pixel_buffer_manager_v1_interface.name, interface) == 0) {
		wlpstate->sp_buffer_manager = wl_registry_bind(registry, name, &wp_single_pixel_buffer_manager_v1_interface, 1);
		return true;
	} else if (strcmp(wp_viewporter_interface.name, interface) == 0) {
		wlpstate->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	}
	return false;
}

int main (int argc, char *argv[]) {
	struct wlpavuo_state wlpstate = {
		.width = 620,
		.height = 640,
		.mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELL,
		.nwl = {
			.xdg_app_id = "wlpavuoverlay",
			.events.global_add = handle_global,
		}
	};
	int opt;
	while ((opt = getopt(argc, argv, "dxpsISw:h:BTLRH")) != -1) {
		switch (opt) {
			case 'd':
				wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_LAYERSHELLFULL;
				break;
			case 'x':
				wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
				break;
			case 'p': wlpstate.use_pipewire = true; break;
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
			case 'H':
				wlpstate.dynamic_height = true;
				break;
			case 'S':
				wlpstate.no_seat = true;
				break;
			case 'I':
				wlpstate.stdin_input = true;
				break;
			case 'B':
				wlpstate.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
				break;
			case 'T':
				wlpstate.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
				break;
			case 'L':
				wlpstate.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
				break;
			case 'R':
				wlpstate.anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
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
		}
	}
	if (nwl_wayland_init(&wlpstate.nwl)) {
		fprintf(stderr, "failed to init, bailing!\n");
		return 1;
	}
	if (wlpstate.nwl.wl.compositor) {
		if (wlpstate.nwl.wl.layer_shell == NULL) {
			wlpstate.mode = WLPAVUO_SURFACE_LAYER_MODE_XDGSHELL;
		}
		create_surface(&wlpstate);
	}
	nwl_wayland_run(&wlpstate.nwl);
	if (wlpstate.stdin_input) {
		nwl_poll_del_fd(&wlpstate.nwl, 0);
	}
	if (wlpstate.sp_buffer_manager) {
		wp_single_pixel_buffer_manager_v1_destroy(wlpstate.sp_buffer_manager);
	}
	if (wlpstate.viewporter) {
		wp_viewporter_destroy(wlpstate.viewporter);
	}
	nwl_wayland_uninit(&wlpstate.nwl);
	return 0;
}

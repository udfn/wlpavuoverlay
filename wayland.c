#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wlpavuoverlay.h"
#include "surface.h"
#include "wlr-layer-shell.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1.h"
#include "viewporter.h"

static void handle_wm_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	UNUSED(data);
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	handle_wm_ping
};

static void handle_output_geometry(
		void *data,
		struct wl_output *wl_output,
		int32_t x,
		int32_t y,
		int32_t physical_width,
		int32_t physical_height,
		int32_t subpixel,
		const char *make,
		const char *model,
		int32_t transform) {
	// don't care
	UNUSED(data);
	UNUSED(wl_output);
	UNUSED(x);
	UNUSED(y);
	UNUSED(physical_width);
	UNUSED(physical_height);
	UNUSED(subpixel);
	UNUSED(make);
	UNUSED(model);
	UNUSED(transform);
}
static void handle_output_mode(void *data,
		struct wl_output *wl_output,
		uint32_t flags,
		int32_t width,
		int32_t height,
		int32_t refresh) {
	// don't care
	UNUSED(data);
	UNUSED(wl_output);
	UNUSED(flags);
	UNUSED(width);
	UNUSED(height);
	UNUSED(refresh);
}
static void handle_output_done(
		void *data,
		struct wl_output *wl_output) {
	// don't care
	UNUSED(data);
	UNUSED(wl_output);
}
static void handle_output_scale(
		void *data,
		struct wl_output *wl_output,
		int32_t factor) {
	UNUSED(wl_output);
	struct wlpavuo_output *output = (struct wlpavuo_output*)data;
	output->scale = factor;
}

static const struct wl_output_listener output_listener = {
	handle_output_geometry,
	handle_output_mode,
	handle_output_done,
	handle_output_scale
};

static void wlpavuo_output_create(struct wl_output *output, struct wlpavuo_state *state) {
	struct wlpavuo_output *wlpvoutput = calloc(1,sizeof(struct wlpavuo_output));
	wl_output_add_listener(output, &output_listener, wlpvoutput);
	wl_output_set_user_data(output, wlpvoutput);
	wl_list_insert(&state->outputs, &wlpvoutput->link);
}

static void handle_global_add(void *data, struct wl_registry *reg,
		uint32_t name, const char *interface, uint32_t version) {
	struct wlpavuo_state *state = (struct wlpavuo_state*) data;
	if (strcmp(interface,wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, version);
	} else if (strcmp(interface,zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(reg,name,&zwlr_layer_shell_v1_interface, version);
	} else if (strcmp(interface,xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = wl_registry_bind(reg, name, &xdg_wm_base_interface, version);
		xdg_wm_base_add_listener(state->xdg_wm_base, &wm_base_listener, state);
	} else if (strcmp(interface,wl_seat_interface.name) == 0) {
		struct wl_seat *newseat = wl_registry_bind(reg, name, &wl_seat_interface, version);
		wlpavuo_seat_create(newseat, state);
	} else if (strcmp(interface,wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(reg, name, &wl_shm_interface, wl_shm_interface.version);
	} else if (strcmp(interface,zxdg_decoration_manager_v1_interface.name) == 0) {
		state->decoration = wl_registry_bind(reg,name,&zxdg_decoration_manager_v1_interface, zxdg_decoration_manager_v1_interface.version);
	} else if (strcmp(interface,wl_output_interface.name) == 0) {
		struct wl_output *newoutput = wl_registry_bind(reg, name, &wl_output_interface, version);
		wlpavuo_output_create(newoutput, state);
	} else if (strcmp(interface,wp_viewporter_interface.name) == 0) {
		state->viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, version);
	} else if (strcmp(interface,wl_subcompositor_interface.name) == 0) {
		state->subcompositor = wl_registry_bind(reg, name, &wl_subcompositor_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	UNUSED(data);
	UNUSED(reg);
	UNUSED(name);
	// don't care
}

static const struct wl_registry_listener reg_listener = {
	handle_global_add,
	handle_global_remove
};

void wlpavuo_wayland_run(struct wlpavuo_state *state) {
	while (wl_display_dispatch(state->display) != -1 && state->num_surfaces) {
		if (state->destroy_surfaces) {
			struct wlpavuo_surface *surface;
			struct wlpavuo_surface *surfacetmp;
			wl_list_for_each_safe(surface, surfacetmp, &state->surfaces, link) {
				if (surface->flags & WLPAVUO_SURFACE_FLAG_DESTROY) {
					wlpavuo_surface_destroy(surface);
				}
			}
			state->destroy_surfaces = 0;
		}
	}
}

char wlpavuo_wayland_init(struct wlpavuo_state *state) {
	state->display = wl_display_connect(NULL);
	if (!state->display) {
		fprintf(stderr,"couldn't connect to Wayland display.\n");
		return 1;
	}
	state->registry = wl_display_get_registry(state->display);
	wl_list_init(&state->seats);
	wl_list_init(&state->outputs);
	wl_list_init(&state->surfaces);
	wl_registry_add_listener(state->registry, &reg_listener, state);
	wl_display_roundtrip(state->display);
	return 0;
}

void wlpavuo_wayland_uninit(struct wlpavuo_state *state) {
	struct wlpavuo_surface *surface;
	struct wlpavuo_surface *surfacetmp;
	wl_list_for_each_safe(surface, surfacetmp, &state->surfaces, link) {
		wlpavuo_surface_destroy(surface);
	}
	wlpavuoverlay_egl_uninit(state);
	struct wlpavuo_seat *seat;
	struct wlpavuo_seat *seattmp;
	xkb_context_unref(state->keyboard_context);
	wl_list_for_each_safe(seat,seattmp, &state->seats, link) {
		wl_list_remove(&seat->link);
		wlpavuo_seat_destroy(seat);
	}
	struct wlpavuo_output *output;
	struct wlpavuo_output *outputtmp;
	wl_list_for_each_safe(output,outputtmp, &state->outputs, link) {
		wl_list_remove(&output->link);
		free(output);
	}

	if (state->cursor_theme) {
		wl_cursor_theme_destroy(state->cursor_theme);
	}
	wl_display_disconnect(state->display);
}

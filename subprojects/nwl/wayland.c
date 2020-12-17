#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "xdg-shell.h"
#include "xdg-decoration-unstable-v1.h"
#include "viewporter.h"

struct nwl_poll {
	int efd;
	int dfd;
	int numfds;
	struct epoll_event *ev;
};

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
	struct nwl_output *output = data;
	output->scale = factor;
}

static const struct wl_output_listener output_listener = {
	handle_output_geometry,
	handle_output_mode,
	handle_output_done,
	handle_output_scale
};

static void nwl_output_destroy(void *glob) {
	struct nwl_output *output = glob;
	wl_list_remove(&output->link);
	wl_output_set_user_data(output->output, NULL);
	wl_output_destroy(output->output);
	free(output);
}

static void nwl_output_create(struct wl_output *output, struct nwl_state *state, uint32_t name) {
	struct nwl_output *wlpvoutput = calloc(1,sizeof(struct nwl_output));
	wl_output_add_listener(output, &output_listener, wlpvoutput);
	wl_output_set_user_data(output, wlpvoutput);
	wlpvoutput->output = output;
	wl_list_insert(&state->outputs, &wlpvoutput->link);
	struct nwl_global *glob = calloc(1,sizeof(struct nwl_global));
	glob->global = wlpvoutput;
	glob->name = name;
	glob->impl.destroy = nwl_output_destroy;
	wl_list_insert(&state->globals, &glob->link);
}

static void *nwl_registry_bind(struct wl_registry *reg, uint32_t name,
		const struct wl_interface *interface, int version ) {
	uint32_t ver = version > interface->version ? interface->version : version;
	return wl_registry_bind(reg, name, interface, ver);
}

static void handle_global_add(void *data, struct wl_registry *reg,
		uint32_t name, const char *interface, uint32_t version) {
	struct nwl_state *state = data;
	if (strcmp(interface,wl_compositor_interface.name) == 0) {
		state->compositor = nwl_registry_bind(reg, name, &wl_compositor_interface, version);
	} else if (strcmp(interface,zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = nwl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, version);
	} else if (strcmp(interface,xdg_wm_base_interface.name) == 0) {
		state->xdg_wm_base = nwl_registry_bind(reg, name, &xdg_wm_base_interface, version);
		xdg_wm_base_add_listener(state->xdg_wm_base, &wm_base_listener, state);
	} else if (strcmp(interface,wl_seat_interface.name) == 0) {
		struct wl_seat *newseat = nwl_registry_bind(reg, name, &wl_seat_interface, version);
		nwl_seat_create(newseat, state, name);
	} else if (strcmp(interface,wl_shm_interface.name) == 0) {
		state->shm = nwl_registry_bind(reg, name, &wl_shm_interface, version);
	} else if (strcmp(interface,zxdg_decoration_manager_v1_interface.name) == 0) {
		state->decoration = nwl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, version);
	} else if (strcmp(interface,wl_output_interface.name) == 0) {
		struct wl_output *newoutput = nwl_registry_bind(reg, name, &wl_output_interface, version);
		nwl_output_create(newoutput, state, name);
	} else if (strcmp(interface,wp_viewporter_interface.name) == 0) {
		state->viewporter = nwl_registry_bind(reg, name, &wp_viewporter_interface, version);
	} else if (strcmp(interface,wl_subcompositor_interface.name) == 0) {
		state->subcompositor = nwl_registry_bind(reg, name, &wl_subcompositor_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	UNUSED(reg);
	struct nwl_state *state = data;
	struct nwl_global *glob;
	wl_list_for_each(glob, &state->globals, link) {
		if (glob->name == name) {
			glob->impl.destroy(glob->global);
			wl_list_remove(&glob->link);
			free(glob);
			return;
		}
	}
}

static const struct wl_registry_listener reg_listener = {
	handle_global_add,
	handle_global_remove
};

void nwl_poll_add_seat(struct nwl_seat *seat) {
	struct nwl_state *state = seat->state;
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* ++state->poll->numfds);
	struct epoll_event ep;
	ep.data.ptr = seat;
	ep.events = EPOLLIN;
	epoll_ctl(state->poll->efd, EPOLL_CTL_ADD, seat->keyboard_repeat_fd, &ep);
}

void nwl_poll_remove_seat(struct nwl_seat *seat) {
	struct nwl_state *state = seat->state;
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* --state->poll->numfds);
	epoll_ctl(state->poll->efd, EPOLL_CTL_DEL, seat->keyboard_repeat_fd, NULL);
}

static void nwl_wayland_poll_display(struct nwl_state *state) {
	wl_display_dispatch(state->display);
	if (state->destroy_surfaces) {
		struct nwl_surface *surface;
		struct nwl_surface *surfacetmp;
		wl_list_for_each_safe(surface, surfacetmp, &state->surfaces, link) {
			if (surface->flags & NWL_SURFACE_FLAG_DESTROY) {
				nwl_surface_destroy(surface);
			}
		}
		state->destroy_surfaces = false;
	}
}

void nwl_wayland_run(struct nwl_state *state) {
	wl_display_flush(state->display);
	// Everything about this seems very flaky.. but it works!
	while (state->num_surfaces) {
		wl_display_flush(state->display);
		int nfds = epoll_wait(state->poll->efd, state->poll->ev, state->poll->numfds, -1);
		if (nfds == -1) {
			perror("error while polling");
			return;
		}
		for (int i = 0; i < nfds;i++) {
			if (state->poll->ev[i].data.fd == state->poll->dfd) {
				nwl_wayland_poll_display(state);
			} else {
				nwl_seat_send_key_repeat(state->poll->ev[i].data.ptr);
			}
		}
	}
}

char nwl_wayland_init(struct nwl_state *state) {
	state->display = wl_display_connect(NULL);
	if (!state->display) {
		fprintf(stderr,"couldn't connect to Wayland display.\n");
		return 1;
	}
	state->registry = wl_display_get_registry(state->display);
	wl_list_init(&state->seats);
	wl_list_init(&state->outputs);
	wl_list_init(&state->surfaces);
	wl_list_init(&state->globals);
	wl_registry_add_listener(state->registry, &reg_listener, state);
	wl_display_roundtrip(state->display);
	state->poll = calloc(1,sizeof(struct nwl_poll));
	state->poll->efd = epoll_create1(0);
	state->poll->ev = calloc(1,sizeof(struct epoll_event));
	struct epoll_event ep = { 0 };
	state->poll->dfd = wl_display_get_fd(state->display);
	ep.data.fd = state->poll->dfd;
	ep.events = EPOLLIN;
	state->poll->numfds = 1;
	epoll_ctl(state->poll->efd, EPOLL_CTL_ADD, state->poll->dfd, &ep);
	return 0;
}

void nwl_wayland_uninit(struct nwl_state *state) {
	struct nwl_surface *surface;
	struct nwl_surface *surfacetmp;
	wl_list_for_each_safe(surface, surfacetmp, &state->surfaces, link) {
		nwl_surface_destroy(surface);
	}
	nwl_egl_uninit(state);
	xkb_context_unref(state->keyboard_context);
	struct nwl_global *glob;
	struct nwl_global *globtmp;
	wl_list_for_each_safe(glob, globtmp, &state->globals, link) {
		glob->impl.destroy(glob->global);
		wl_list_remove(&glob->link);
		free(glob);
	}
	if (state->cursor_theme) {
		wl_cursor_theme_destroy(state->cursor_theme);
	}
	wl_display_disconnect(state->display);
	free(state->poll->ev);
	close(state->poll->efd);
	free(state->poll);
}

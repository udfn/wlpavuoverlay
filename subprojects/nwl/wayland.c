#define _POSIX_C_SOURCE 200809L
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
#include "xdg-output-unstable-v1.h"
#include "viewporter.h"

struct nwl_poll {
	int efd;
	int numfds;
	struct epoll_event *ev;
	struct wl_list data; // nwl_poll_data
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

static void handle_xdg_output_logical_position(void *data, struct zxdg_output_v1 *output,
		int32_t x, int32_t y) {
	struct nwl_output *noutput = data;
	UNUSED(output);
	noutput->x = x;
	noutput->y = y;
}

static void handle_xdg_output_logical_size(void *data, struct zxdg_output_v1 *output,
		int32_t width, int32_t height) {
	struct nwl_output *noutput = data;
	UNUSED(output);
	noutput->width = width;
	noutput->height = height;
}

static void handle_xdg_output_done(void *data, struct zxdg_output_v1 *output) {
	// shouldn't be used
	UNUSED(data);
	UNUSED(output);
}

static void handle_xdg_output_name(void *data, struct zxdg_output_v1 *output, const char *name) {
	UNUSED(output);
	struct nwl_output *nwloutput = data;
	if (nwloutput->name) {
		free(nwloutput->name);
	}
	nwloutput->name = strdup(name);
}

static void handle_xdg_output_description(void *data, struct zxdg_output_v1 *output, const char *description) {
	// don't care
	UNUSED(data);
	UNUSED(output);
	UNUSED(description);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	handle_xdg_output_logical_position,
	handle_xdg_output_logical_size,
	handle_xdg_output_done,
	handle_xdg_output_name,
	handle_xdg_output_description
};

static void nwl_output_destroy(void *glob) {
	struct nwl_output *output = glob;
	if (output->state->events.output_destroy) {
		output->state->events.output_destroy(output);
	}
	if (output->name) {
		free(output->name);
	}
	wl_list_remove(&output->link);
	wl_output_set_user_data(output->output, NULL);
	wl_output_destroy(output->output);
	free(output);
}

static void nwl_output_create(struct wl_output *output, struct nwl_state *state, uint32_t name) {
	struct nwl_output *nwloutput = calloc(1,sizeof(struct nwl_output));
	wl_output_add_listener(output, &output_listener, nwloutput);
	wl_output_set_user_data(output, nwloutput);
	nwloutput->output = output;
	nwloutput->state = state;
	wl_list_insert(&state->outputs, &nwloutput->link);
	struct nwl_global *glob = calloc(1,sizeof(struct nwl_global));
	glob->global = nwloutput;
	glob->name = name;
	glob->impl.destroy = nwl_output_destroy;
	if (state->xdg_output_manager) {
		nwloutput->xdg_output = zxdg_output_manager_v1_get_xdg_output(state->xdg_output_manager, output);
		zxdg_output_v1_add_listener(nwloutput->xdg_output, &xdg_output_listener, nwloutput);
	}
	wl_list_insert(&state->globals, &glob->link);
	// Maybe move this to handle_output_done?
	if (state->events.output_new) {
		state->events.output_new(nwloutput);
	}
}

static void *nwl_registry_bind(struct wl_registry *reg, uint32_t name,
		const struct wl_interface *interface, int version ) {
	uint32_t ver = version > interface->version ? interface->version : version;
	return wl_registry_bind(reg, name, interface, ver);
}

static void handle_global_add(void *data, struct wl_registry *reg,
		uint32_t name, const char *interface, uint32_t version) {
	struct nwl_state *state = data;
	if (state->events.global_add &&
			state->events.global_add(state,reg,name,interface,version)) {
		return;
	}
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
	} else if (strcmp(interface,zxdg_output_manager_v1_interface.name) == 0) {
		state->xdg_output_manager = nwl_registry_bind(reg, name, &zxdg_output_manager_v1_interface, version);
	}
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	UNUSED(reg);
	struct nwl_state *state = data;
	if (state->events.global_remove) {
		state->events.global_remove(state,reg,name);
	}
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

struct nwl_poll_data {
	struct wl_list link;
	int fd;
	void *userdata;
	nwl_poll_callback_t callback;
};

void nwl_poll_add_fd(struct nwl_state *state, int fd,
		nwl_poll_callback_t callback, void *data) {
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* ++state->poll->numfds);
	struct epoll_event ep;
	struct nwl_poll_data *polldata = calloc(1,sizeof(struct nwl_poll_data));
	wl_list_insert(&state->poll->data, &polldata->link);
	polldata->userdata = data;
	polldata->fd = fd;
	polldata->callback = callback;
	ep.data.ptr = polldata;
	ep.events = EPOLLIN;
	epoll_ctl(state->poll->efd, EPOLL_CTL_ADD, fd, &ep);
}

void nwl_poll_del_fd(struct nwl_state *state, int fd) {
	state->poll->ev = realloc(state->poll->ev, sizeof(struct epoll_event)* --state->poll->numfds);
	epoll_ctl(state->poll->efd, EPOLL_CTL_DEL, fd, NULL);
	struct nwl_poll_data *data;
	wl_list_for_each(data, &state->poll->data, link) {
		if (data->fd == fd) {
			wl_list_remove(&data->link);
			free(data);
			return;
		}
	}
}

static void nwl_wayland_poll_display(struct nwl_state *state, void *data) {
	UNUSED(data);
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
	while (state->run_with_zero_surfaces || state->num_surfaces) {
		wl_display_flush(state->display);
		int nfds = epoll_wait(state->poll->efd, state->poll->ev, state->poll->numfds, -1);
		if (nfds == -1) {
			perror("error while polling");
			return;
		}
		for (int i = 0; i < nfds;i++) {
			struct nwl_poll_data *data = state->poll->ev[i].data.ptr;
			data->callback(state, data->userdata);
		}
	}
}

char nwl_wayland_init(struct nwl_state *state) {
	state->poll = calloc(1,sizeof(struct nwl_poll));
	wl_list_init(&state->seats);
	wl_list_init(&state->outputs);
	wl_list_init(&state->surfaces);
	wl_list_init(&state->globals);
	wl_list_init(&state->poll->data);
	wl_list_init(&state->subs);
	state->display = wl_display_connect(NULL);
	if (!state->display) {
		fprintf(stderr, "couldn't connect to Wayland display.\n");
		return 1;
	}
	state->poll->efd = epoll_create1(0);
	nwl_poll_add_fd(state, wl_display_get_fd(state->display), nwl_wayland_poll_display, NULL);
	state->registry = wl_display_get_registry(state->display);
	wl_registry_add_listener(state->registry, &reg_listener, state);
	wl_display_roundtrip(state->display);
	if (state->xdg_output_manager) {
		// Extra roundtrip so output information is properly filled in
		wl_display_roundtrip(state->display);
	}
	return 0;
}

void nwl_wayland_uninit(struct nwl_state *state) {
	struct nwl_surface *surface;
	struct nwl_surface *surfacetmp;
	wl_list_for_each_safe(surface, surfacetmp, &state->surfaces, link) {
		nwl_surface_destroy(surface);
	}
	struct nwl_state_sub *sub;
	struct nwl_state_sub *subtmp;
	wl_list_for_each_safe(sub, subtmp, &state->subs, link) {
		sub->impl->destroy(sub->data);
		wl_list_remove(&sub->link);
		free(sub);
	}
	nwl_egl_uninit(state);
	if (state->keyboard_context) {
		xkb_context_unref(state->keyboard_context);
	}
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
	nwl_poll_del_fd(state, wl_display_get_fd(state->display));
	wl_display_disconnect(state->display);
	free(state->poll->ev);
	close(state->poll->efd);
	free(state->poll);
}

// These should be moved into state.c or something..
void *nwl_state_get_sub(struct nwl_state *state, struct nwl_state_sub_impl *subimpl) {
	struct nwl_state_sub *sub;
	wl_list_for_each(sub, &state->subs, link) {
		if (sub->impl == subimpl) {
			return sub->data;
		}
	}
	return NULL; // EVIL NULL POINTER!
}
void nwl_state_add_sub(struct nwl_state *state, struct nwl_state_sub_impl *subimpl, void *data) {
	struct nwl_state_sub *sub = calloc(1, sizeof(struct nwl_state_sub));
	sub->data = data;
	sub->impl = subimpl;
	wl_list_insert(&state->subs, &sub->link);
}

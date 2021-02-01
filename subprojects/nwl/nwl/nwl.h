#ifndef _NWL_NWL_H
#define _NWL_NWL_H
#define UNUSED(x) (void)(x)
#include <stdbool.h>
#include <EGL/egl.h>
#include <cairo/cairo.h>
#include <wayland-util.h>
#include "seat.h"

struct nwl_global {
	struct wl_list link;
	uint32_t name;
	void *global;
	struct {
		void (*destroy)(void* global);
	} impl;
};

struct nwl_output {
	struct nwl_state *state;
	struct wl_output *output;
	struct zxdg_output_v1 *xdg_output;
	struct wl_list link;
	int scale;
	char *name;
};

struct nwl_state {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *xdg_wm_base;
	struct zxdg_output_manager_v1 *xdg_output_manager;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_decoration_manager_v1 *decoration;
	struct wp_viewporter *viewporter;
	struct wl_subcompositor *subcompositor;
	struct wl_list seats; // nwl_seat
	struct wl_list outputs; // nwl_output
	struct wl_list surfaces; // nwl_surface
	struct wl_list globals; // nwl_global
	struct xkb_context *keyboard_context;
	struct {
		EGLDisplay display;
		EGLConfig config;
		EGLContext context;
		// This shouldn't be here..
		cairo_device_t *cairo_dev;
		char inited;
	} egl;

	struct wl_cursor_theme *cursor_theme;
	uint32_t cursor_theme_size;
	uint32_t num_surfaces;
	bool run_with_zero_surfaces;
	bool destroy_surfaces;
	struct nwl_poll *poll;
	struct {
		// A wild output appears!
		void (*output_new)(struct nwl_output *output);
		// The output is about to go away!
		void (*output_destroy)(struct nwl_output *output);
		// A wild global appears! Return true to not have nwl take care of it!
		bool (*global_add)(struct nwl_state *state, struct wl_registry *registry, uint32_t name,
			const char *interface, uint32_t version);
		// The global disappears!
		void (*global_remove)(struct nwl_state *statem, struct wl_registry *registry, uint32_t name);
	} events;
	void *userdata;
};

typedef void (*nwl_poll_callback_t)(struct nwl_state *state, void* data);

char nwl_wayland_init(struct nwl_state *state);
void nwl_wayland_uninit(struct nwl_state *state);
void nwl_wayland_run(struct nwl_state *state);
void nwl_poll_add_fd(struct nwl_state *state, int fd, // pollin, pollout, edge trigger, etc?
	nwl_poll_callback_t callback, void *data);
void nwl_poll_del_fd(struct nwl_state *state, int fd);
// To be very inconsistent, this returns true on success..
bool nwl_egl_try_init(struct nwl_state *state);
void nwl_egl_uninit(struct nwl_state *state);

#endif

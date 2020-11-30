#define _POSIX_C_SOURCE 200112L
#include <wayland-client.h>
#include <wayland-egl.h>
#include <epoxy/egl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <cairo/cairo-gl.h>
#include "wlr-layer-shell.h"
#include "xdg-shell-client-protocol.h"
#include "wlpavuoverlay.h"
#include "ui.h"
#include "surface.h"

static void randname(char *buf) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i) {
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

static int create_shm_file(void) {
	int retries = 100;
	do {
		char name[] = "/wlpao_shm-XXXXXX";
		randname(name + sizeof(name) - 7);
		--retries;
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);
	return -1;
}

static int allocate_shm_file(size_t size) {
	int fd = create_shm_file();
	if (fd < 0)
		return -1;
	int ret;
	do {
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

struct wlpavuo_surface_shm {
	int fd;
	uint8_t *data;
	struct wl_shm_pool *pool;
	size_t size;
	int32_t stride;
	char *name;
	struct wl_buffer *buffer;
};

static void destroy_shm_pool(struct wlpavuo_surface_shm *shm) {
	wl_shm_pool_destroy(shm->pool);
	munmap(shm->data, shm->size);
	close(shm->fd);
}

static void allocate_wl_shm_pool(struct wlpavuo_surface *surface) {
	struct wlpavuo_surface_shm *shm = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, scaled_width);
	shm->stride = stride;
	int pool_size = scaled_height * stride * 2;
	if (shm->fd) {
		destroy_shm_pool(shm);
	}
	int fd = allocate_shm_file(pool_size);
	shm->fd = fd;
	shm->data = mmap(NULL,pool_size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
	shm->pool = wl_shm_create_pool(surface->state->shm, fd, pool_size);
	surface->cairo_surface = cairo_image_surface_create_for_data(shm->data,CAIRO_FORMAT_ARGB32,scaled_width,scaled_height,stride);
	return;
}

static void shm_surface_destroy(struct wlpavuo_surface *surface) {
	struct wlpavuo_surface_shm *shm = surface->render.data;
	cairo_surface_destroy(surface->cairo_surface);
	destroy_shm_pool(shm);
	free(surface->render.data);
}

static void shm_surface_swapbuffers(struct wlpavuo_surface *surface) {
	struct wlpavuo_surface_shm *shm = surface->render.data;
	if (shm->buffer) {
		wl_buffer_destroy(shm->buffer);
	}
	uint32_t scaled_width = surface->width*surface->scale;
	uint32_t scaled_height = surface->height*surface->scale;
	shm->buffer = wl_shm_pool_create_buffer(shm->pool, 0, scaled_width,scaled_height, shm->stride, WL_SHM_FORMAT_ARGB8888);
	wl_surface_attach(surface->wl.surface, shm->buffer, 0,0);
	wl_surface_damage(surface->wl.surface, 0,0, scaled_width,scaled_height);
	wl_surface_commit(surface->wl.surface);
}

static void surface_render_set_shm(struct wlpavuo_surface *surface) {
	if (surface->render.data) {
		surface->render.impl.destroy(surface);
	}
	surface->render.data = calloc(sizeof(struct wlpavuo_surface_shm),1);
	surface->render.impl.destroy = shm_surface_destroy;
	surface->render.impl.applysize = allocate_wl_shm_pool;
	surface->render.impl.swapbuffers = shm_surface_swapbuffers;
}
struct wlpavuo_surface_egl {
	struct wl_egl_window *window;
	EGLSurface surface;
};

static void egl_surface_destroy(struct wlpavuo_surface *surface) {
	struct wlpavuo_surface_egl *egl = surface->render.data;
	cairo_surface_destroy(surface->cairo_surface);
	wl_egl_window_destroy(egl->window);
	eglDestroySurface(surface->state->egl.display, egl->surface);
	free(surface->render.data);
}

static void egl_surface_applysize(struct wlpavuo_surface *surface) {
	struct wlpavuo_surface_egl *egl = surface->render.data;
	uint32_t scaled_width = surface->width * surface->scale;
	uint32_t scaled_height = surface->height * surface->scale;
	if (!egl->window) {
		struct wlpavuo_surface_egl *egl = surface->render.data;
		egl->window = wl_egl_window_create(surface->wl.surface, scaled_width, scaled_height);
		egl->surface = eglCreatePlatformWindowSurfaceEXT(surface->state->egl.display, surface->state->egl.config, egl->window, NULL);
		surface->cairo_surface = cairo_gl_surface_create_for_egl(surface->state->egl.cairo_dev, egl->surface, scaled_width,scaled_height);
	} else {
		wl_egl_window_resize(egl->window, scaled_width,scaled_height,0,0);
		cairo_gl_surface_set_size(surface->cairo_surface, scaled_width,scaled_height);
	}
}

static void egl_surface_swapbuffers(struct wlpavuo_surface *surface) {
	cairo_gl_surface_swapbuffers(surface->cairo_surface);
}

static void surface_render_set_egl(struct wlpavuo_surface *surface) {
	if (surface->render.data) {
		surface->render.impl.destroy(surface);
	}
	surface->render.data = calloc(sizeof(struct wlpavuo_surface_egl),1);
	surface->render.impl.destroy = egl_surface_destroy;
	surface->render.impl.applysize = egl_surface_applysize;
	surface->render.impl.swapbuffers = egl_surface_swapbuffers;

}

struct wl_callback_listener callback_listener;

static void cb_done(void *data, struct wl_callback *cb, uint32_t cb_data) {
	UNUSED(cb_data);
	wl_callback_destroy(cb);
	struct wlpavuo_surface *surf = (struct wlpavuo_surface*)data;
	if (surf->flags & WLPAVUO_SURFACE_FLAG_NEEDS_DRAW) {
		surf->flags = surf->flags & ~WLPAVUO_SURFACE_FLAG_NEEDS_DRAW;
		surf->impl.render(surf);
	}
	surf->wl.frame_cb = NULL;
	if (surf->flags & WLPAVUO_SURFACE_FLAG_NEEDS_DRAW) {
		surf->wl.frame_cb = wl_surface_frame(surf->wl.surface);
		wl_callback_add_listener(surf->wl.frame_cb, &callback_listener, surf);
		wl_surface_commit(surf->wl.surface);
	}
}

struct wl_callback_listener callback_listener = {
	cb_done
};

struct wlpavuo_surface_output {
	struct wl_list link;
	struct wlpavuo_output *output;
};

static void surface_set_scale_from_outputs(struct wlpavuo_surface *surf) {
	int scale = 1;
	struct wlpavuo_surface_output *surfoutput;
	wl_list_for_each(surfoutput, &surf->outputs, link) {
		if (surfoutput->output->scale > scale) {
			scale = surfoutput->output->scale;
		}
	}
	surf->scale = scale;
	wlpavuo_surface_apply_size(surf);
	surf->flags |= WLPAVUO_SURFACE_FLAG_NEEDS_DRAW;
}

static void handle_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct wlpavuo_output *wlpvoutput = wl_output_get_user_data(output);
	struct wlpavuo_surface *surf = (struct wlpavuo_surface*)data;
	struct wlpavuo_surface_output *surfoutput = calloc(1,sizeof(struct wlpavuo_surface_output));
	surfoutput->output = wlpvoutput;
	wl_list_insert(&surf->outputs, &surfoutput->link);
	if (!(surf->flags & WLPAVUO_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}
static void handle_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output) {
	UNUSED(surface);
	struct wlpavuo_output *wlpvoutput = wl_output_get_user_data(output);
	struct wlpavuo_surface *surf = (struct wlpavuo_surface*)data;
	struct wlpavuo_surface_output *surfoutput;

	wl_list_for_each(surfoutput, &surf->outputs, link) {
		if (surfoutput->output == wlpvoutput) {
			break;
		}
	}
	wl_list_remove(&surfoutput->link);
	free(surfoutput);
	if (!(surf->flags & WLPAVUO_SURFACE_FLAG_NO_AUTOSCALE)) {
		surface_set_scale_from_outputs(surf);
	}
}
static const struct wl_surface_listener surface_listener = {
	handle_surface_enter,
	handle_surface_leave
};

struct wlpavuo_surface *wlpavuo_surface_create(struct wlpavuo_state *state, char *title, enum wlpavuo_surface_renderer renderer) {
	struct wlpavuo_surface *newsurf = (struct wlpavuo_surface*)calloc(1,sizeof(struct wlpavuo_surface));
	newsurf->state = state;
	newsurf->wl.surface = wl_compositor_create_surface(state->compositor);
	newsurf->scale = 1;
	newsurf->title = title;
	newsurf->flags = WLPAVUO_SURFACE_FLAG_CSD;
	wl_list_init(&newsurf->subsurfaces);
	wl_list_init(&newsurf->outputs);
	wl_list_insert(&state->surfaces, &newsurf->link);
	wl_surface_set_user_data(newsurf->wl.surface, newsurf);
	wl_surface_set_buffer_scale(newsurf->wl.surface, newsurf->scale);
	wl_surface_add_listener(newsurf->wl.surface, &surface_listener, newsurf);
	if (renderer == WLPAVUO_SURFACE_RENDER_EGL) {
		switch (state->egl.inited) {
			case 2:
				renderer = WLPAVUO_SURFACE_RENDER_SHM;
				break;
			case 1:
				break;
			case 0:
			{
				if (wlpavuoverlay_egl_init(state)) {
					state->egl.inited = 2;
					wlpavuoverlay_egl_uninit(state);
				}
				else if (cairo_device_status(state->egl.cairo_dev) != CAIRO_STATUS_SUCCESS) {
					fprintf(stderr,"couldn't get Cairo device status %i\n",cairo_device_status(state->egl.cairo_dev));
					renderer = WLPAVUO_SURFACE_RENDER_SHM;
				}
			}
		}
	}
	// Uh.. what's up with that switch up there?
	if (renderer == WLPAVUO_SURFACE_RENDER_SHM) {
		surface_render_set_shm(newsurf);
	} else {
		surface_render_set_egl(newsurf);
	}
	state->num_surfaces++;
	return newsurf;
}

void wlpavuo_surface_destroy(struct wlpavuo_surface *surface) {
	// clear any focuses
	wlpavuo_seat_clear_focus(surface);
	// destroy any possible subsurfaces as well
	struct wlpavuo_surface *subsurf;
	struct wlpavuo_surface *subsurftmp;
	if (!surface->parent) {
		wl_list_remove(&surface->link);
	}
	wl_list_for_each_safe(subsurf, subsurftmp, &surface->subsurfaces, sublink) {
		wl_list_remove(&subsurf->sublink);
		wlpavuo_surface_destroy(subsurf);
	}
	if (surface->impl.destroy) {
		surface->impl.destroy(surface);
	}
	surface->render.impl.destroy(surface);
	struct wlpavuo_surface_output *surfoutput;
	struct wlpavuo_surface_output *surfoutputtmp;
	wl_list_for_each_safe(surfoutput, surfoutputtmp, &surface->outputs, link) {
		wl_list_remove(&surfoutput->link);
		free(surfoutput);
	}
	if (surface->wl.xdg_toplevel) {
		xdg_toplevel_destroy(surface->wl.xdg_toplevel);
		xdg_surface_destroy(surface->wl.xdg_surface);
	}
	else if (surface->wl.layer_surface) {
		zwlr_layer_surface_v1_destroy(surface->wl.layer_surface);
	}
	wl_surface_destroy(surface->wl.surface);
	surface->state->num_surfaces--;
	free(surface);
}

void wlpavuo_surface_destroy_later(struct wlpavuo_surface *surface) {
	surface->flags = WLPAVUO_SURFACE_FLAG_DESTROY;
	surface->state->destroy_surfaces = 1;
	if (surface->wl.frame_cb) {
		wl_callback_destroy(surface->wl.frame_cb);
	}
}

void wlpavuo_surface_set_size(struct wlpavuo_surface *surface, uint32_t width, uint32_t height) {
	surface->desired_height = height;
	surface->desired_width = width;
	if (surface->wl.layer_surface) {
		zwlr_layer_surface_v1_set_size(surface->wl.layer_surface, width, height);
	} else if (surface->wl.subsurface) {
		// Subsurfaces can always be their desired size I think..
		surface->width = width;
		surface->height = height;
	}
}

void wlpavuo_surface_apply_size(struct wlpavuo_surface *surface) {
	wl_surface_set_buffer_scale(surface->wl.surface, surface->scale);
	surface->render.impl.applysize(surface);
	struct wlpavuo_surface *sub;
	wl_list_for_each(sub, &surface->subsurfaces, sublink) {
		sub->scale = surface->scale;
		wlpavuo_surface_apply_size(sub);
	}
}

void wlpavuo_surface_swapbuffers(struct wlpavuo_surface *surface) {
	surface->frame++;
	surface->render.impl.swapbuffers(surface);
}

void wlpavuo_surface_set_need_draw(struct wlpavuo_surface *surface, bool render) {
	if (surface->parent) {
		wlpavuo_surface_set_need_draw(surface->parent,render);
	} else {
		if (surface->wl.frame_cb) {
			surface->flags |= WLPAVUO_SURFACE_FLAG_NEEDS_DRAW;
			return;
		}
		surface->wl.frame_cb = wl_surface_frame(surface->wl.surface);
		wl_callback_add_listener(surface->wl.frame_cb, &callback_listener, surface);
		wl_surface_commit(surface->wl.surface);
		if (render) {
			surface->impl.render(surface);
		} else {
			surface->flags |= WLPAVUO_SURFACE_FLAG_NEEDS_DRAW;
		}
	}
}

void wlpavuo_surface_add_subsurface(struct wlpavuo_surface *parent, struct wlpavuo_surface *surface) {
	surface->wl.subsurface = wl_subcompositor_get_subsurface(surface->state->subcompositor,
			surface->wl.surface, parent->wl.surface);
	surface->parent = parent;
	wl_list_insert(&parent->subsurfaces, &surface->sublink);
	// hack, remove subsurfaces from the main surfaces list
	wl_list_remove(&surface->link);
}

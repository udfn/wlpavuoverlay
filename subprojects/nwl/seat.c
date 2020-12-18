#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <linux/input-event-codes.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "nwl/nwl.h"
#include "nwl/surface.h"

static void handle_keyboard_keymap(void *data, struct wl_keyboard *wl_keyboard, uint32_t format,
		int32_t fd, uint32_t size) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		fprintf(stderr, "I don't understand keymap format %i :(\n", format);
		close(fd);
		return;
	}
	char *kbmap = mmap(NULL,size,PROT_READ,MAP_PRIVATE, fd, 0);
	if (seat->keyboard_keymap) {
		xkb_keymap_unref(seat->keyboard_keymap);
		xkb_state_unref(seat->keyboard_state);
	}
	seat->keyboard_keymap = xkb_keymap_new_from_string(seat->state->keyboard_context, kbmap,
		XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	seat->keyboard_state = xkb_state_new(seat->keyboard_keymap);
	munmap(kbmap, size);
	close(fd);
}

static void dispatch_keyboard_event(struct nwl_keyboard_event *event, struct nwl_surface *surface) {
	if (surface->impl.input_keyboard) {
		surface->impl.input_keyboard(surface,event);
	}
	event->type = 0;
}

void nwl_seat_send_key_repeat(struct nwl_state *state, void *data) {
	UNUSED(state);
	struct nwl_seat *seat = data;
	uint64_t expirations;
	read(seat->keyboard_repeat_fd,&expirations,sizeof(uint64_t));
	if (seat->keyboard_focus) {
		seat->keyboard_event->type = NWL_KEYBOARD_EVENT_KEYREPEAT;
		dispatch_keyboard_event(seat->keyboard_event,seat->keyboard_focus);
	}
}

static void handle_keyboard_enter(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface,
		struct wl_array *keys) {
	UNUSED(wl_keyboard);
	UNUSED(keys);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	seat->keyboard_focus = wl_surface_get_user_data(surface);
	seat->keyboard_event->serial = serial;
	seat->keyboard_event->focus = 1;
	seat->keyboard_event->type = NWL_KEYBOARD_EVENT_FOCUS;
	dispatch_keyboard_event(seat->keyboard_event, seat->keyboard_focus);
	// TODO: held keys
}

static void handle_keyboard_leave(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		struct wl_surface *surface) {
	UNUSED(surface);
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (!seat->keyboard_focus) {
		return;
	}
	seat->keyboard_event->serial = serial;
	seat->keyboard_event->focus = 0;
	seat->keyboard_event->type = NWL_KEYBOARD_EVENT_FOCUS;
	dispatch_keyboard_event(seat->keyboard_event, seat->keyboard_focus);
	seat->keyboard_focus = NULL;
	struct itimerspec timer = { 0 };
	timerfd_settime(seat->keyboard_repeat_fd, 0, &timer, NULL);
}

static void handle_keyboard_key(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t time,
		uint32_t key,
		uint32_t state) {
	UNUSED(wl_keyboard);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (!seat->keyboard_focus) {
		return;
	}
	xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->keyboard_state, key+8);
	seat->keyboard_event->serial = serial;
	seat->keyboard_event->type = state == WL_KEYBOARD_KEY_STATE_PRESSED ? 
			NWL_KEYBOARD_EVENT_KEYDOWN : NWL_KEYBOARD_EVENT_KEYUP;
	seat->keyboard_event->keysym = keysym;
	dispatch_keyboard_event(seat->keyboard_event, seat->keyboard_focus);
	struct itimerspec timer = { 0 };
	if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		timer.it_value.tv_nsec = seat->keyboard_repeat_delay * 1000000;
		timer.it_interval.tv_nsec = seat->keyboard_repeat_rate * 1000000;
	}
	timerfd_settime(seat->keyboard_repeat_fd, 0, &timer, NULL);
}

static void handle_keyboard_modifiers(
		void *data,
		struct wl_keyboard *wl_keyboard,
		uint32_t serial,
		uint32_t mods_depressed,
		uint32_t mods_latched,
		uint32_t mods_locked,
		uint32_t group) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	xkb_state_update_mask(seat->keyboard_state, mods_depressed, mods_latched, mods_locked, 0,0,group);
	seat->keyboard_event->serial = serial;
}

static void handle_keyboard_repeat(
		void *data,
		struct wl_keyboard *wl_keyboard,
		int32_t rate,
		int32_t delay) {
	UNUSED(wl_keyboard);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	seat->keyboard_repeat_rate = rate;
	seat->keyboard_repeat_delay = delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	handle_keyboard_keymap,
	handle_keyboard_enter,
	handle_keyboard_leave,
	handle_keyboard_key,
	handle_keyboard_modifiers,
	handle_keyboard_repeat
};

static void nwl_seat_set_pointer_cursor(struct nwl_seat *seat, struct nwl_surface *surface, const char *cursor) {
	if ((int)seat->state->cursor_theme_size != surface->scale*24) {
		if (seat->state->cursor_theme) {
			wl_cursor_theme_destroy(seat->state->cursor_theme);
		} else {
			seat->pointer_surface = wl_compositor_create_surface(seat->state->compositor);
		}
		seat->state->cursor_theme = wl_cursor_theme_load(NULL, 24 * surface->scale, seat->state->shm);
		seat->state->cursor_theme_size = 24 * surface->scale;
		wl_surface_set_buffer_scale(seat->pointer_surface, surface->scale);
	}
	seat->pointer_cursor = wl_cursor_theme_get_cursor(seat->state->cursor_theme, cursor);
	struct wl_buffer *cursbuffer = wl_cursor_image_get_buffer(seat->pointer_cursor->images[0]);
	wl_surface_attach(seat->pointer_surface, cursbuffer, 0,0);
	wl_surface_commit(seat->pointer_surface);
	wl_pointer_set_cursor(seat->pointer, seat->pointer_event->serial, seat->pointer_surface,
		seat->pointer_cursor->images[0]->hotspot_x, seat->pointer_cursor->images[0]->hotspot_y);
}

static void handle_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	UNUSED(pointer);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	struct nwl_surface *nwlsurf = wl_surface_get_user_data(surface);
	seat->pointer_event->surface_x = surface_x;
	seat->pointer_event->surface_y = surface_y;
	seat->pointer_event->focus = 1;
	seat->pointer_focus = nwlsurf;
	seat->pointer_event->serial = serial;
	seat->pointer_event->changed |= NWL_POINTER_EVENT_MOTION | NWL_POINTER_EVENT_FOCUS;
	nwl_seat_set_pointer_cursor(seat, nwlsurf, "left_ptr");
}

static void handle_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
	UNUSED(pointer);
	UNUSED(surface);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (seat->pointer_focus) {
		seat->pointer_event->serial = serial;
		seat->pointer_event->focus = 0;
		seat->pointer_event->changed |= NWL_POINTER_EVENT_FOCUS;
	}
}

static void handle_pointer_motion(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	seat->pointer_event->changed |= NWL_POINTER_EVENT_MOTION;
	seat->pointer_event->surface_x = surface_x;
	seat->pointer_event->surface_y = surface_y;
}

static char map_linuxmbutton_to_nwl(uint32_t button) {
	switch (button) {
		case BTN_LEFT:
			return NWL_MOUSE_LEFT;
		case BTN_RIGHT:
			return NWL_MOUSE_RIGHT;
		case BTN_MIDDLE:
			return NWL_MOUSE_MIDDLE;
		case BTN_EXTRA:
			return NWL_MOUSE_FORWARD;
		case BTN_SIDE:
			return NWL_MOUSE_BACK;
		default:
			fprintf(stderr,"Unknown mouse button %x\n",button);
			return 0;
	}
}

static void handle_pointer_button(void *data,
		struct wl_pointer *wl_pointer,
		uint32_t serial,
		uint32_t time,
		uint32_t button,
		uint32_t state) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	char nwl_mbutton = map_linuxmbutton_to_nwl(button);
	seat->pointer_event->changed |= NWL_POINTER_EVENT_BUTTON;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		seat->pointer_event->buttons |= nwl_mbutton;
	} else {
		char cur = seat->pointer_event->buttons;
		seat->pointer_event->buttons = cur & ~nwl_mbutton;
	}
	seat->pointer_event->serial = serial;
}

static void handle_pointer_axis(void *data,
	struct wl_pointer *wl_pointer,
	uint32_t time,
	uint32_t axis,
	wl_fixed_t value) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = data;
	if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
		seat->pointer_event->axis_hori += value;
	} else {
		seat->pointer_event->axis_vert += value;
	}
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}

static void handle_pointer_frame(void *data, struct wl_pointer *wl_pointer) {
	struct nwl_seat *seat = data;
	UNUSED(wl_pointer);
	if (seat->pointer_focus && seat->pointer_focus->impl.input_pointer) {
		// Hmm.. how does this behave if the pointer moves across two surfaces in the same frame?
		seat->pointer_focus->impl.input_pointer(seat->pointer_focus, seat->pointer_event);
		if (seat->pointer_event->changed & NWL_POINTER_EVENT_FOCUS && !seat->pointer_event->focus) {
			seat->pointer_focus = NULL;
		}
	}
	seat->pointer_event->changed = 0;
	seat->pointer_event->axis_discrete_hori = 0;
	seat->pointer_event->axis_discrete_vert = 0;
	seat->pointer_event->axis_hori = 0;
	seat->pointer_event->axis_vert = 0;
	seat->pointer_event->axis_source = 0;
}

static char wl_axis_source_to_nwl(uint32_t axis_source) {
	switch (axis_source) {
		case WL_POINTER_AXIS_SOURCE_WHEEL:
			return NWL_AXIS_SOURCE_WHEEL;
		case WL_POINTER_AXIS_SOURCE_CONTINUOUS:
			return NWL_AXIS_SOURCE_CONTINUOUS;
		case WL_POINTER_AXIS_SOURCE_FINGER:
			return NWL_AXIS_SOURCE_FINGER;
		case WL_POINTER_AXIS_SOURCE_WHEEL_TILT:
			return NWL_AXIS_SOURCE_WHEEL_TILT;
		default:
			return 0;
	}
}

static char wl_axis_to_nwl(uint32_t axis) {
	switch(axis) {
		case WL_POINTER_AXIS_HORIZONTAL_SCROLL:
			return NWL_AXIS_HORIZONTAL;
		case WL_POINTER_AXIS_VERTICAL_SCROLL:
			return NWL_AXIS_VERTICAL;
		default:
			return 0;
	}
}

static void handle_pointer_axis_source(void *data, struct wl_pointer *wl_pointer, uint32_t axis_source) {
	UNUSED(wl_pointer);
	struct nwl_seat *seat = data;
	seat->pointer_event->axis_source |= wl_axis_source_to_nwl(axis_source);
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}
static void handle_pointer_axis_stop(void *data, struct wl_pointer *wl_pointer, uint32_t time,
		uint32_t axis) {
	UNUSED(wl_pointer);
	UNUSED(time);
	struct nwl_seat *seat = data;
	seat->pointer_event->axis_stop |= wl_axis_to_nwl(axis);
}
static void handle_pointer_axis_discrete(void *data, struct wl_pointer *wl_pointer, uint32_t axis,
		int32_t discrete) {
	UNUSED(wl_pointer);
	struct nwl_seat *seat = (struct nwl_seat*)data;
	if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
		seat->pointer_event->axis_discrete_vert += discrete;
	} else {
		seat->pointer_event->axis_discrete_hori += discrete;
	}
	seat->pointer_event->changed |= NWL_POINTER_EVENT_AXIS;
}
static const struct wl_pointer_listener pointer_listener = {
	handle_pointer_enter,
	handle_pointer_leave,
	handle_pointer_motion,
	handle_pointer_button,
	handle_pointer_axis,
	handle_pointer_frame,
	handle_pointer_axis_source,
	handle_pointer_axis_stop,
	handle_pointer_axis_discrete
};
/*
static void handle_touch_down(void *data,
		struct wl_touch *wl_touch,
		uint32_t serial,
		uint32_t time,
		struct wl_surface *surface,
		int32_t id,
		wl_fixed_t x,
		wl_fixed_t y) {
	// bla
}

static void handle_touch_up(void *data,
		struct wl_touch *wl_touch,
		uint32_t serial,
		uint32_t time,
		int32_t id) {
	// bla
}

static void handle_touch_motion(void *data,
		struct wl_touch *wl_touch,
		uint32_t time,
		int32_t id,
		wl_fixed_t x,
		wl_fixed_t y) {
	// bla
}

static void handle_touch_frame(void *data,
		struct wl_touch *wl_touch) {
	// bla
}

static void handle_touch_cancel(void *data,
		struct wl_touch *wl_touch) {
	// bla
}

static void handle_touch_shape(void *data,
		struct wl_touch *wl_touch,
		int32_t id,
		wl_fixed_t major,
		wl_fixed_t minor) {
	// bla
}

static void handle_touch_orientation(void *data,
		struct wl_touch *wl_touch,
		int32_t id,
		wl_fixed_t orientation) {
	// bla
}

static const struct wl_touch_listener touch_listener = {
	handle_touch_down,
	handle_touch_up,
	handle_touch_motion,
	handle_touch_frame,
	handle_touch_cancel,
	handle_touch_shape,
	handle_touch_orientation
};
*/
static void handle_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
	struct nwl_seat *nwseat = data;
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		nwseat->keyboard = wl_seat_get_keyboard(seat);
		if (!nwseat->state->keyboard_context) {
			nwseat->state->keyboard_context = xkb_context_new(0);
		}
		nwseat->keyboard_event = calloc(1,sizeof(struct nwl_keyboard_event));
		nwseat->keyboard_repeat_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
		nwl_poll_add_fd(nwseat->state, nwseat->keyboard_repeat_fd, nwl_seat_send_key_repeat, nwseat);
		wl_keyboard_add_listener(nwseat->keyboard, &keyboard_listener, data);
	}
	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		nwseat->pointer = wl_seat_get_pointer(seat);
		nwseat->pointer_event = calloc(1,sizeof(struct nwl_pointer_event));
		nwseat->pointer_event->seat = nwseat;
		wl_pointer_add_listener(nwseat->pointer, &pointer_listener, data);
	}
	/*
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		nwseat->touch = wl_seat_get_touch(seat);
		wl_touch_add_listener(nwseat->touch, &touch_listener, data);
	}
	*/
}

static void handle_seat_name(void *data, struct wl_seat *seat, const char *name) {
	UNUSED(seat);
	struct nwl_seat *nwseat = (struct nwl_seat*)data;
	nwseat->name = strdup(name);
}

static const struct wl_seat_listener seat_listener = {
	handle_seat_capabilities,
	handle_seat_name
};

void nwl_seat_clear_focus(struct nwl_surface *surface) {
	struct nwl_seat *seat;
	struct nwl_state *state = surface->state;
	wl_list_for_each(seat, &state->seats, link) {
		if (seat->pointer_focus == surface) {
			seat->pointer_focus = NULL;
		}
		if (seat->keyboard_focus == surface) {
			seat->keyboard_focus = NULL;
		}
	}
}

void nwl_seat_destroy(void *data) {
	struct nwl_seat *seat = data;
	wl_list_remove(&seat->link);
	free(seat->name);
	if (seat->keyboard_keymap) {
		xkb_keymap_unref(seat->keyboard_keymap);
	}
	if (seat->keyboard_state) {
		xkb_state_unref(seat->keyboard_state);
	}
	if (seat->pointer_event) {
		free(seat->pointer_event);
	}
	if (seat->keyboard_event) {
		nwl_poll_del_fd(seat->state, seat->keyboard_repeat_fd);
		free(seat->keyboard_event);
		close(seat->keyboard_repeat_fd);
	}
	wl_seat_destroy(seat->wl_seat);
	free(seat);
}

void nwl_seat_create(struct wl_seat *wlseat, struct nwl_state *state, uint32_t name) {
	struct nwl_seat *nwlseat = calloc(1,sizeof(struct nwl_seat));
	nwlseat->state = state;
	nwlseat->wl_seat = wlseat;
	wl_list_insert(&state->seats, &nwlseat->link);
	wl_seat_add_listener(wlseat, &seat_listener, nwlseat);
	struct nwl_global *glob = calloc(1,sizeof(struct nwl_global));
	glob->global = nwlseat;
	glob->name = name;
	glob->impl.destroy = nwl_seat_destroy;
	wl_list_insert(&state->globals, &glob->link);
}

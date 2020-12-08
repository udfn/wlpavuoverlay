#ifndef _WLPV_SEAT_H_
#define _WLPV_SEAT_H_
#include <xkbcommon/xkbcommon.h>
#include <stdint.h>
#include <wayland-util.h>
struct nwl_seat {
	struct nwl_state *state;
	struct wl_list link;
	struct wl_seat *wl_seat;

	struct wl_keyboard *keyboard;
	struct xkb_keymap *keyboard_keymap;
	struct xkb_state *keyboard_state;
	struct nwl_surface *keyboard_focus;
	int32_t keyboard_repeat_rate;
	int32_t keyboard_repeat_delay;
	int keyboard_repeat_fd;
	struct nwl_keyboard_event *keyboard_event;

	struct wl_touch *touch;
	struct nwl_surface *touch_focus;
	uint32_t touch_serial;

	struct wl_pointer *pointer;
	struct nwl_surface *pointer_focus;
	struct wl_cursor *pointer_cursor;
	struct wl_surface *pointer_surface;
	struct nwl_pointer_event *pointer_event;
	char *name;
};

enum nwl_pointer_event_changed {
	NWL_POINTER_EVENT_FOCUS = 1 << 0,
	NWL_POINTER_EVENT_BUTTON = 1 << 1,
	NWL_POINTER_EVENT_MOTION = 1 << 2,
	NWL_POINTER_EVENT_AXIS = 1 < 3,
};

enum nwl_pointer_buttons {
	NWL_MOUSE_LEFT = 1 << 0,
	NWL_MOUSE_MIDDLE = 1 << 1,
	NWL_MOUSE_RIGHT = 1 << 2,
	NWL_MOUSE_BACK = 1 << 3,
	NWL_MOUSE_FORWARD = 1 << 4,
};

// basically wl_pointer_axis_source converted to bitflags
enum nwl_pointer_axis_source {
	NWL_AXIS_SOURCE_WHEEL = 1 << 0,
	NWL_AXIS_SOURCE_FINGER = 1 << 1,
	NWL_AXIS_SOURCE_CONTINUOUS = 1 << 2,
	NWL_AXIS_SOURCE_WHEEL_TILT = 1 << 3,
};

enum nwl_pointer_axis {
	NWL_AXIS_VERTICAL = 1 << 0,
	NWL_AXIS_HORIZONTAL = 1 << 1,
};

struct nwl_pointer_event {
	struct nwl_seat *seat;
	char changed; // nwl_seat_pointer_event_changed
	uint32_t serial;
	wl_fixed_t surface_x;
	wl_fixed_t surface_y;
	char buttons; // nwl_pointer_buttons
	int32_t axis_discrete_vert;
	int32_t axis_discrete_hori;
	wl_fixed_t axis_hori;
	wl_fixed_t axis_vert;
	char axis_source; // nwl_pointer_axis_source
	char axis_stop; // nwl_pointer_axis
	char focus;
};

enum nwl_keyboard_event_type {
	NWL_KEYBOARD_EVENT_FOCUS,
	NWL_KEYBOARD_EVENT_KEYDOWN,
	NWL_KEYBOARD_EVENT_KEYUP,
	NWL_KEYBOARD_EVENT_KEYREPEAT,
	NWL_KEYBOARD_EVENT_MODIFIERS,
};

struct nwl_keyboard_event {
	struct nwl_seat *seat;
	char type; // nwl_keyboard_event_type
	char focus;
	xkb_keysym_t keysym;
	uint32_t serial;
};

void nwl_seat_create(struct wl_seat *wlseat, struct nwl_state *state);
void nwl_seat_send_key_repeat(struct nwl_seat *seat);
void nwl_seat_destroy(struct nwl_seat *seat);
void nwl_seat_clear_focus(struct nwl_surface *surface);

#endif

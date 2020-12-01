#ifndef _WLPV_SEAT_H_
#define _WLPV_SEAT_H_
#include <xkbcommon/xkbcommon.h>
#include <stdint.h>
#include <wayland-util.h>
struct wlpavuo_seat {
	struct wlpavuo_state *state;
	struct wl_list link;
	struct wl_seat *wl_seat;

	struct wl_keyboard *keyboard;
	struct xkb_keymap *keyboard_keymap;
	struct xkb_state *keyboard_state;
	struct wlpavuo_surface *keyboard_focus;
	int32_t keyboard_repeat_rate;
	int32_t keyboard_repeat_delay;
	int keyboard_repeat_fd;
	struct wlpavuo_keyboard_event *keyboard_event;

	struct wl_touch *touch;
	struct wlpavuo_surface *touch_focus;
	uint32_t touch_serial;

	struct wl_pointer *pointer;
	struct wlpavuo_surface *pointer_focus;
	struct wl_cursor *pointer_cursor;
	struct wl_surface *pointer_surface;
	struct wlpavuo_pointer_event *pointer_event;
	char *name;
};

enum wlpavuo_pointer_event_changed {
	WLPAVUO_POINTER_EVENT_FOCUS = 1 << 0,
	WLPAVUO_POINTER_EVENT_BUTTON = 1 << 1,
	WLPAVUO_POINTER_EVENT_MOTION = 1 << 2,
	WLPAVUO_POINTER_EVENT_AXIS = 1 < 3,
};

enum wlpavuo_pointer_buttons {
	WLPAVUO_MOUSE_LEFT = 1 << 0,
	WLPAVUO_MOUSE_MIDDLE = 1 << 1,
	WLPAVUO_MOUSE_RIGHT = 1 << 2,
	WLPAVUO_MOUSE_BACK = 1 << 3,
	WLPAVUO_MOUSE_FORWARD = 1 << 4,
};

// basically wl_pointer_axis_source converted to bitflags
enum wlpavuo_pointer_axis_source {
	WLPAVUO_AXIS_SOURCE_WHEEL = 1 << 0,
	WLPAVUO_AXIS_SOURCE_FINGER = 1 << 1,
	WLPAVUO_AXIS_SOURCE_CONTINUOUS = 1 << 2,
	WLPAVUO_AXIS_SOURCE_WHEEL_TILT = 1 << 3,
};

enum wlpavuo_pointer_axis {
	WLPAVUO_AXIS_VERTICAL = 1 << 0,
	WLPAVUO_AXIS_HORIZONTAL = 1 << 1,
};

struct wlpavuo_pointer_event {
	struct wlpavuo_seat *seat;
	char changed; // wlpavuo_seat_pointer_event_changed
	uint32_t serial;
	wl_fixed_t surface_x;
	wl_fixed_t surface_y;
	char buttons; // wlpavuo_pointer_buttons
	int32_t axis_discrete_vert;
	int32_t axis_discrete_hori;
	wl_fixed_t axis_hori;
	wl_fixed_t axis_vert;
	char axis_source; // wlpavuo_pointer_axis_source
	char axis_stop; // wlpavuo_pointer_axis
	char focus;
};

enum wlpavuo_keyboard_event_type {
	WLPAVUO_KEYBOARD_EVENT_FOCUS,
	WLPAVUO_KEYBOARD_EVENT_KEYDOWN,
	WLPAVUO_KEYBOARD_EVENT_KEYUP,
	WLPAVUO_KEYBOARD_EVENT_KEYREPEAT,
	WLPAVUO_KEYBOARD_EVENT_MODIFIERS,
};

struct wlpavuo_keyboard_event {
	struct wlpavuo_seat *seat;
	char type; // wlpavuo_keyboard_event_type
	char focus;
	xkb_keysym_t keysym;
	uint32_t serial;
};

void wlpavuo_seat_create(struct wl_seat *wlseat, struct wlpavuo_state *state);
void wlpavuo_seat_send_key_repeat(struct wlpavuo_seat *seat);
void wlpavuo_seat_destroy(struct wlpavuo_seat *seat);
void wlpavuo_seat_clear_focus(struct wlpavuo_surface *surface);

#endif

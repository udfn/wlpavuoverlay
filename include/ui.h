#ifndef _WLPV_UI_H
#define _WLPV_UI_H
#include <wlpavuoverlay.h>

struct wlpavuo_ui;
void wlpavuo_ui_input_pointer(struct wlpavuo_surface *surface, struct wlpavuo_pointer_event *event);
void wlpavuo_ui_input_keyboard(struct wlpavuo_surface *surface, struct wlpavuo_keyboard_event *event);
char wlpavuo_ui_run(struct wlpavuo_surface *surface, cairo_t *cr);
void wlpavuo_ui_destroy(struct wlpavuo_surface *surface);
#endif

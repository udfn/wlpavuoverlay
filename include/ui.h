#ifndef _WLPV_UI_H
#define _WLPV_UI_H
#include <nwl/nwl.h>
struct wlpavuo_state {
	bool use_pipewire;
};
void wlpavuo_ui_input_pointer(struct nwl_surface *surface, struct nwl_pointer_event *event);
void wlpavuo_ui_input_keyboard(struct nwl_surface *surface, struct nwl_keyboard_event *event);
char wlpavuo_ui_run(struct nwl_surface *surface, cairo_t *cr);
void wlpavuo_ui_destroy(struct nwl_surface *surface);

#endif

#ifndef _WLPV_PULSE_H
#define _WLPV_PULSE_H
#include <wayland-util.h>
enum wlpavuo_pulse_status {
	WLPAVUO_PULSE_STATUS_READY,
	WLPAVUO_PULSE_STATUS_CONNECTING,
	WLPAVUO_PULSE_STATUS_FAILED
};
enum wlpavuo_pulse_info_flags {
	WLPAVUO_PULSE_MUTED = 1 << 0,
	WLPAVUO_PULSE_STREAM_INPUT = 1 << 1,
};

struct wlpavuo_pulse_client_stream {
	struct wl_list link;
	uint32_t id;
	char flags;
	char *name;
	unsigned long volume;
	uint8_t channels;
};

struct wlpavuo_pulse_client {
	struct wl_list link;
	uint32_t id;
	char *name;
	uint32_t streams_count;
	struct wl_list streams; // wlpavuo_pulse_client_stream
};

struct wlpavuo_pulse_sink {
	struct wl_list link;
	uint32_t id;
	char flags;
	char *name;
	unsigned long volume;
	uint8_t channels;
};


// Indicates that something has changed in pulse state, and the interface should be updated
typedef void (*wlpavuo_pulse_update_cb_t)(void *data);
enum wlpavuo_pulse_status wlpavuo_pulse_init();
void wlpavuo_pulse_uninit();
void wlpavuo_pulse_set_update_callback(wlpavuo_pulse_update_cb_t cb, void *data);
void wlpavuo_pulse_lock();
void wlpavuo_pulse_unlock();
void wlpavuo_pulse_set_stream_volume(struct wlpavuo_pulse_client_stream *stream, uint32_t vol);
void wlpavuo_pulse_set_stream_mute(struct wlpavuo_pulse_client_stream *stream, char mute);
void wlpavuo_pulse_set_sink_volume(struct wlpavuo_pulse_sink *sink, uint32_t vol);
void wlpavuo_pulse_set_sink_mute(struct wlpavuo_pulse_sink *sink, char mute);

struct wl_list *wlpavuo_pulse_get_clients(); // wlpavuo_pulse_client
struct wl_list *wlpavuo_pulse_get_sinks(); // wlpavuo_pulse_client

#endif

#ifndef _WLPV_PULSE_H
#define _WLPV_PULSE_H
#include <wayland-util.h>

enum wlpavuo_audio_status {
	WLPAVUO_AUDIO_STATUS_READY,
	WLPAVUO_AUDIO_STATUS_CONNECTING,
	WLPAVUO_AUDIO_STATUS_FAILED
};
enum wlpavuo_audio_info_flags {
	WLPAVUO_AUDIO_MUTED = 1 << 0,
};

struct wlpavuo_audio_client_stream {
	struct wl_list link;
	uint32_t id;
	char flags;
	char *name;
	unsigned long volume;
	uint8_t channels;
	void *data;
};

struct wlpavuo_audio_client {
	struct wl_list link;
	uint32_t id;
	char *name;
	uint32_t streams_count;
	struct wl_list streams; // wlpavuo_audio_client_stream
};

struct wlpavuo_audio_sink {
	struct wl_list link;
	uint32_t id;
	char flags;
	char *name;
	unsigned long volume;
	uint8_t channels;
	void *data;
};

typedef void (*wlpavuo_audio_update_cb_t)(void *data);

struct wlpavuo_audio_impl {
	const char* (*get_name)();
	enum wlpavuo_audio_status (*init)();
	void (*uninit)();
	void (*set_update_callback)(wlpavuo_audio_update_cb_t cb, void *data);
	void (*lock)();
	void (*unlock)();
	void (*set_stream_volume)(struct wlpavuo_audio_client_stream *stream, uint32_t vol);
	void (*set_stream_mute)(struct wlpavuo_audio_client_stream *stream, char mute);
	void (*set_sink_volume)(struct wlpavuo_audio_sink *sink, uint32_t vol);
	void (*set_sink_mute)(struct wlpavuo_audio_sink *sink, char mute);
	struct wl_list*(*get_clients)(); // wlpavuo_audio_client
	struct wl_list*(*get_sinks)(); // wlpavuo_audio_sink
	int (*get_fd)();
	void (*iterate)();
};
#ifdef HAVE_PULSEAUDIO
const struct wlpavuo_audio_impl* wlpavuo_audio_get_pa();
#endif
#ifdef HAVE_PIPEWIRE
// But PipeWire isn't only audio..
const struct wlpavuo_audio_impl* wlpavuo_audio_get_pw();
#endif
#endif

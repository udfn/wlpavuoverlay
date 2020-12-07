#include <pipewire/pipewire.h>
#include "audio.h"
// TODO!
#define UNIMPLEMENTED() fprintf(stderr,"PW unimplemented func %s\n", __FUNCTION__);
struct pipewire_state {
	enum wlpavuo_audio_status status;
	struct pw_main_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_registry *registry;
	struct pw_thread_loop *thread;
	struct spa_hook reg_listener;
};
static struct pipewire_state pw_state = {0};

static void reg_event_global(void *data, uint32_t id, uint32_t permissions,
		const char *type, uint32_t version, const struct spa_dict *props) {
	printf("global %u %s\n",id,type);
}
static void reg_event_global_rm(void *data, uint32_t id) {
	printf("global RM %u\n",id);
}

static const struct pw_registry_events reg_events = {
	PW_VERSION_REGISTRY_EVENTS,
	reg_event_global,
	reg_event_global_rm
};

static const char* pipewire_get_name() {
	return "PipeWire";
}

static enum wlpavuo_audio_status pipewire_init() {
	if (!pw_state.loop) {
		pw_init(NULL,NULL);
		pw_state.loop = pw_main_loop_new(NULL);
		pw_state.context = pw_context_new(pw_main_loop_get_loop(pw_state.loop), NULL,0);
		pw_state.core = pw_context_connect(pw_state.context, NULL,0);
		pw_state.registry = pw_core_get_registry(pw_state.core, PW_VERSION_REGISTRY, 0);
		pw_state.thread = pw_thread_loop_new_full(pw_main_loop_get_loop(pw_state.loop), "wlpavuo pipewire thread", NULL);
		spa_zero(pw_state.reg_listener);
		pw_registry_add_listener(pw_state.registry,&pw_state.reg_listener,&reg_events, NULL);
		pw_thread_loop_start(pw_state.thread);
		pw_state.status = WLPAVUO_AUDIO_STATUS_CONNECTING;
	}
	return pw_state.status;
}

static void pipewire_uninit() {
	pw_proxy_destroy((struct pw_proxy*)pw_state.registry);
	pw_core_disconnect(pw_state.core);
	pw_context_destroy(pw_state.context);
	pw_thread_loop_stop(pw_state.thread);
	pw_thread_loop_destroy(pw_state.thread);
	pw_main_loop_destroy(pw_state.loop);
}

static void pipewire_set_update_callback(wlpavuo_audio_update_cb_t cb, void *data) {
	UNIMPLEMENTED();
	return;
}

static void pipewire_lock() {
	pw_thread_loop_lock(pw_state.thread);
}

static void pipewire_unlock() {
	pw_thread_loop_unlock(pw_state.thread);
}

static void pipewire_set_stream_volume(struct wlpavuo_audio_client_stream *stream, uint32_t vol) {
	UNIMPLEMENTED();
}

static void pipewire_set_stream_mute(struct wlpavuo_audio_client_stream *stream, char mute) {
	UNIMPLEMENTED();
}

static void pipewire_set_sink_volume(struct wlpavuo_audio_sink *sink, uint32_t vol) {
	UNIMPLEMENTED();
}

static void pipewire_set_sink_mute(struct wlpavuo_audio_sink *sink, char mute) {
	UNIMPLEMENTED();
}

static struct wl_list* pipewire_get_clients() {
	UNIMPLEMENTED();
	return NULL;
}

static struct wl_list* pipewire_get_sinks() {
	UNIMPLEMENTED();
	return NULL;
}

static const struct wlpavuo_audio_impl pw_impl = {
	pipewire_get_name,
	pipewire_init,
	pipewire_uninit,
	pipewire_set_update_callback,
	pipewire_lock,
	pipewire_unlock,
	pipewire_set_stream_volume,
	pipewire_set_stream_mute,
	pipewire_set_sink_volume,
	pipewire_set_sink_mute,
	pipewire_get_clients,
	pipewire_get_sinks
};

const struct wlpavuo_audio_impl* wlpavuo_audio_get_pw() {
	return &pw_impl;
}

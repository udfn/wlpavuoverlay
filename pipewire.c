#define _POSIX_C_SOURCE 200809L
#include <pipewire/pipewire.h>
#include "audio.h"
// TODO!
#define UNIMPLEMENTED() fprintf(stderr,"PW unimplemented func %s\n", __FUNCTION__);
#define UNUSED(x) (void)(x)

enum pipewire_global_type {
	PIPEWIRE_GLOBAL_TYPE_CLIENT,
	PIPEWIRE_GLOBAL_TYPE_SINK,
	PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM
};

struct pipewire_global {
	struct wl_list link; // globals
	uint32_t id;
	enum pipewire_global_type type;
	void *data; // depends on type
};

struct pipewire_global_stream {
	struct wlpavuo_audio_client_stream *streamdata;
	struct pipewire_global *client;
};

struct pipewire_state {
	enum wlpavuo_audio_status status;
	struct pw_main_loop *loop;
	struct pw_context *context;
	struct pw_core *core;
	struct pw_registry *registry;
	struct pw_thread_loop *thread;
	struct spa_hook reg_listener;
	struct spa_hook core_listener;
	// could probably make do with just the globals list
	struct wl_list clients;
	struct wl_list sinks;
	struct wl_list globals;

	wlpavuo_audio_update_cb_t update_cb;
	void *cb_data;
};
static struct pipewire_state pw_state = {0};

static struct pipewire_global* find_pw_global(uint32_t id) {
	struct pipewire_global *glob;
	wl_list_for_each(glob, &pw_state.globals, link) {
		if (glob->id == id) {
			return glob;
		}
	}
	return NULL;
}

static void add_pw_sink(uint32_t id, const struct spa_dict *props) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_SINK;
	// In the future this will def be pipewire_global_sink or something..
	struct wlpavuo_audio_sink *sinkdata = calloc(1,sizeof(struct wlpavuo_audio_sink));
	newglob->data = sinkdata;
	const char *name = spa_dict_lookup(props, "node.description");
	sinkdata->name = name ? strdup(name) : "<no name>";
	sinkdata->flags = 0;
	sinkdata->volume = 50000;
	wl_list_insert(&pw_state.globals, &newglob->link);
	wl_list_insert(&pw_state.sinks, &sinkdata->link);
}

static void add_pw_client_stream(uint32_t id, const struct spa_dict *props) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM;
	const char *clientidstring = spa_dict_lookup(props,"client.id");
	uint32_t clientid;
	struct pipewire_global *glob = NULL;
	if (clientidstring) {
		clientid = strtoul(clientidstring,NULL,10);
		glob = find_pw_global(clientid);
	}
	//struct wlpavuo_audio_client_stream *stream = calloc(1,sizeof(struct pipewire_global_stream));
	struct wlpavuo_audio_client_stream *streamdata = calloc(1,sizeof(struct wlpavuo_audio_client_stream));
	streamdata->name = strdup("whatever");
	streamdata->volume = 50000;
	if (glob) {
		struct wlpavuo_audio_client *clientdata = glob->data;
		clientdata->streams_count++;
		wl_list_insert(&clientdata->streams, &streamdata->link);
	}
	newglob->data = streamdata;
	wl_list_insert(&pw_state.globals, &newglob->link);
}

static void add_pw_client(uint32_t id, const struct spa_dict *props) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_CLIENT;
	struct wlpavuo_audio_client *clientdata = calloc(1,sizeof(struct wlpavuo_audio_client));
	newglob->data = clientdata;
	const char *name = spa_dict_lookup(props, "application.name");
	clientdata->name = name ? strdup(name) : "<no name>";
	wl_list_init(&clientdata->streams);
	wl_list_insert(&pw_state.globals, &newglob->link);
	wl_list_insert(&pw_state.clients, &clientdata->link);
}

static void fire_pipewire_callback() {
	pw_state.update_cb(pw_state.cb_data);
}

static void reg_event_global(void *data, uint32_t id, uint32_t permissions,
		const char *type, uint32_t version, const struct spa_dict *props) {
	UNUSED(data);
	UNUSED(version);
	UNUSED(permissions);
	if (props == NULL) {
		return;
	}
	if (strcmp(type,PW_TYPE_INTERFACE_Node) == 0) {
		const char *class = spa_dict_lookup(props, "media.class");
		if (class != NULL) {
			if (strcmp(class,"Audio/Sink") == 0) {
				add_pw_sink(id,props);
				fire_pipewire_callback();
				return;
			}
			else if (strcmp(class,"Stream/Output/Audio") == 0) {
				add_pw_client_stream(id,props);
				fire_pipewire_callback();
				return;
			}
		}
	} else if (strcmp(type,PW_TYPE_INTERFACE_Client) == 0) {
		add_pw_client(id,props);
		fire_pipewire_callback();
		return;
	}
}
static void reg_event_global_rm(void *data, uint32_t id) {
	UNUSED(data);
	struct pipewire_global *global;
	wl_list_for_each(global, &pw_state.globals, link) {
		if (global->id == id) {
			wl_list_remove(&global->link);
			switch(global->type) {
				case PIPEWIRE_GLOBAL_TYPE_SINK: {
					struct wlpavuo_audio_sink *sink = global->data;
					wl_list_remove(&sink->link);
					free(sink->name);
					free(sink);
					break;
				}
				case PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM: {
					struct wlpavuo_audio_client_stream *stream = global->data;
					wl_list_remove(&stream->link);
					free(stream->name);
					free(stream);
					break;
				}
				case PIPEWIRE_GLOBAL_TYPE_CLIENT: {
					struct wlpavuo_audio_client *client = global->data;
					wl_list_remove(&client->link);
					free(client->name);
					free(client);
					break;
				}
				default:break;
			}
			fire_pipewire_callback();
			free(global);
			return;
		}
	}
}

static const struct pw_registry_events reg_events = {
	PW_VERSION_REGISTRY_EVENTS,
	reg_event_global,
	reg_event_global_rm
};
static void core_event_done(void *data, uint32_t id, int seq) {
	UNUSED(data);
	UNUSED(id);
	UNUSED(seq);
	pw_state.status = WLPAVUO_AUDIO_STATUS_READY;
}
static const struct pw_core_events core_events = {
	PW_VERSION_CORE_EVENTS,
	.done = core_event_done,
};

static const char* pipewire_get_name() {
	return "PipeWire";
}

static enum wlpavuo_audio_status pipewire_init() {
	if (!pw_state.loop) {
		wl_list_init(&pw_state.sinks);
		wl_list_init(&pw_state.clients);
		wl_list_init(&pw_state.globals);
		pw_init(NULL,NULL);
		pw_state.loop = pw_main_loop_new(NULL);
		pw_state.context = pw_context_new(pw_main_loop_get_loop(pw_state.loop), NULL,0);
		pw_state.core = pw_context_connect(pw_state.context, NULL,0);
		pw_state.registry = pw_core_get_registry(pw_state.core, PW_VERSION_REGISTRY, 0);
		pw_state.thread = pw_thread_loop_new_full(pw_main_loop_get_loop(pw_state.loop), "wlpavuo pipewire thread", NULL);
		spa_zero(pw_state.reg_listener);
		spa_zero(pw_state.core_listener);
		pw_registry_add_listener(pw_state.registry,&pw_state.reg_listener,&reg_events, NULL);
		pw_core_add_listener(pw_state.core,&pw_state.core_listener,&core_events,NULL);
		pw_thread_loop_start(pw_state.thread);
		pw_state.status = WLPAVUO_AUDIO_STATUS_CONNECTING;
		pw_core_sync(pw_state.core, PW_ID_CORE, 0);
	}
	return pw_state.status;
}

static void pipewire_uninit() {
	pw_thread_loop_lock(pw_state.thread);
	pw_proxy_destroy((struct pw_proxy*)pw_state.registry);
	pw_core_disconnect(pw_state.core);
	pw_context_destroy(pw_state.context);
	pw_thread_loop_unlock(pw_state.thread);
	pw_thread_loop_stop(pw_state.thread);
	pw_thread_loop_destroy(pw_state.thread);
	pw_main_loop_destroy(pw_state.loop);
}

static void pipewire_set_update_callback(wlpavuo_audio_update_cb_t cb, void *data) {
	pw_state.update_cb = cb;
	pw_state.cb_data = data;
	return;
}

static void pipewire_lock() {
	pw_thread_loop_lock(pw_state.thread);
}

static void pipewire_unlock() {
	pw_thread_loop_unlock(pw_state.thread);
}

static void pipewire_set_stream_volume(struct wlpavuo_audio_client_stream *stream, uint32_t vol) {
	UNUSED(stream);
	UNUSED(vol);
	UNIMPLEMENTED();
}

static void pipewire_set_stream_mute(struct wlpavuo_audio_client_stream *stream, char mute) {
	UNUSED(stream);
	UNUSED(mute);
	UNIMPLEMENTED();
}

static void pipewire_set_sink_volume(struct wlpavuo_audio_sink *sink, uint32_t vol) {
	UNUSED(sink);
	UNUSED(vol);
	UNIMPLEMENTED();
}

static void pipewire_set_sink_mute(struct wlpavuo_audio_sink *sink, char mute) {
	UNUSED(sink);
	UNUSED(mute);
	UNIMPLEMENTED();
}

static struct wl_list* pipewire_get_clients() {
	return &pw_state.clients;
}

static struct wl_list* pipewire_get_sinks() {
	return &pw_state.sinks;
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

#define _POSIX_C_SOURCE 200809L
#include <pipewire/pipewire.h>
// Pulse is only included here for volume conversion purposes
#include <pulse/pulseaudio.h>
#include <spa/pod/parser.h>
#include <spa/pod/builder.h>
#include <spa/param/props.h>
#include <spa/param/param.h>
#include <spa/param/audio/raw.h>
#include "audio.h"
// TODO!
#define UNIMPLEMENTED() fprintf(stderr,"PW unimplemented func %s\n", __FUNCTION__);
#define UNUSED(x) (void)(x)

enum pipewire_global_type {
	PIPEWIRE_GLOBAL_TYPE_CLIENT,
	PIPEWIRE_GLOBAL_TYPE_SINK,
	PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM,
	PIPEWIRE_GLOBAL_TYPE_DEVICE
};

struct pipewire_global {
	struct wl_list link; // globals
	uint32_t id;
	enum pipewire_global_type type;
	void *data; // depends on type
	struct spa_hook hook;
	struct pw_proxy *proxy; // But is it actually a proxy?
};

struct pipewire_sink_node {
	struct pipewire_global *device;
	struct wlpavuo_audio_sink sinkdata;

	// route stuff, maybe shouldn't be here
	uint32_t r_index;
	uint32_t r_device;
	uint32_t r_channels;
	int r_direction;
	char *r_name;
};

struct pipewire_stream_node {
	struct pipewire_global *client;
	struct wlpavuo_audio_client_stream stream;
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

static void node_event_info(void *data, const struct pw_node_info *info) {
	struct pipewire_global *global = data;
	if (global->type == PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM) {
		// Stream name?
		const char *streamname = spa_dict_lookup(info->props, "media.name");
		if (streamname == NULL) {
			return;
		}
		struct pipewire_stream_node *streamdata = global->data;
		if (streamdata->stream.name != NULL) {
			free(streamdata->stream.name);
		}
		streamdata->stream.name = strdup(streamname);
	}
	uint32_t id = SPA_PARAM_Props;
	pw_node_enum_params((struct pw_node*)global->proxy,0,id,0,0,NULL);
	pw_node_subscribe_params((struct pw_node*)global->proxy,&id,1);
}

static void fire_pipewire_callback() {
	pw_state.update_cb(pw_state.cb_data);
}

static void node_event_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next,
		const struct spa_pod *param) {
	UNUSED(seq);
	UNUSED(id);
	UNUSED(index);
	UNUSED(next);
	struct spa_pod_prop *prop;
	struct spa_pod_object *obj = (struct spa_pod_object *) param;
	bool mute;
	float vol[SPA_AUDIO_MAX_CHANNELS] = {0};
	SPA_POD_OBJECT_FOREACH(obj, prop) {
		switch (prop->key) {
			case SPA_PROP_mute:
				spa_pod_get_bool(&prop->value, &mute);
				break;
			case SPA_PROP_channelVolumes:
				spa_pod_copy_array(&prop->value, SPA_TYPE_Float, &vol, SPA_AUDIO_MAX_CHANNELS);
				break;
		}
	}
	struct pipewire_global *glob = data;
	// Convert to PA volume, maybe I should just yoink the function and do it directly here
	unsigned long pavol = pa_sw_volume_from_linear(vol[0]);
	if (glob->type == PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM) {
		struct pipewire_stream_node *streamdata = glob->data;
		streamdata->stream.volume = pavol;
		streamdata->stream.flags = mute ? WLPAVUO_AUDIO_MUTED : 0;
	}
	fire_pipewire_callback();
}

struct pw_node_events node_events = {
	PW_VERSION_NODE_EVENTS,
	node_event_info,
	node_event_param
};

static void device_event_info(void *data, const struct pw_device_info *info) {
	UNUSED(info);
	struct pipewire_global *global = data;
	uint32_t id = SPA_PARAM_Route;
	pw_device_enum_params((struct pw_device*)global->proxy,0,id,0,0,NULL);
	pw_device_subscribe_params((struct pw_device*)global->proxy,&id,1);
}

static void device_event_param(void *data, int seq, uint32_t id, uint32_t index, uint32_t next,
		const struct spa_pod *param) {
	UNUSED(seq);
	UNUSED(id);
	UNUSED(index);
	UNUSED(next);
	bool mute;
	const char *name;
	uint32_t direction;
	int pod_index;
	int device_id;
	uint32_t num_channels;
	float vol[SPA_AUDIO_MAX_CHANNELS];
	struct spa_pod_prop *prop;
	struct spa_pod_object *obj = (struct spa_pod_object *) param;
	SPA_POD_OBJECT_FOREACH(obj, prop) {
		switch (prop->key) {
			case SPA_PARAM_ROUTE_props: {
				struct spa_pod_prop *rprop;
				struct spa_pod_object *objp = (struct spa_pod_object*)&prop->value;
				// This sure doesn't feel like it's gonna cease to function soon
				SPA_POD_OBJECT_FOREACH(objp,rprop) {
					switch (rprop->key) {
						case SPA_PROP_mute:
							spa_pod_get_bool(&rprop->value, &mute);
						break;
						case SPA_PROP_channelVolumes:
							num_channels = spa_pod_copy_array(&rprop->value, SPA_TYPE_Float, &vol, SPA_AUDIO_MAX_CHANNELS);
							break;
					}
				}
			}
			case SPA_PARAM_ROUTE_index:
				spa_pod_get_int(&prop->value,&pod_index);
				break;
			case SPA_PARAM_ROUTE_device:
				spa_pod_get_int(&prop->value,&device_id);
				break;
			case SPA_PARAM_ROUTE_direction:
				spa_pod_get_id(&prop->value,&direction);
				break;
			case SPA_PARAM_ROUTE_name:
				spa_pod_get_string(&prop->value, &name);
				break;
		}
	}
	struct pipewire_global *glob = data;
	if (glob->data) {
		struct pipewire_global *sink = glob->data;
		struct pipewire_sink_node *sinknode = sink->data;
		sinknode->r_device = device_id;
		sinknode->r_index = pod_index;
		sinknode->r_direction = direction;
		sinknode->r_channels = num_channels;
		// uh oh, malloc action
		if (sinknode->r_name != NULL) {
			free(sinknode->r_name);
		}
		sinknode->r_name = strdup(name);
		unsigned long pavol = pa_sw_volume_from_linear(vol[0]);
		sinknode->sinkdata.volume = pavol;
		sinknode->sinkdata.flags = mute ? WLPAVUO_AUDIO_MUTED : 0;
		fire_pipewire_callback();
	}

}

struct pw_device_events device_events = {
	PW_VERSION_DEVICE_EVENTS,
	device_event_info,
	device_event_param
};

static void add_pw_sink(uint32_t id, const struct spa_dict *props, struct pw_node *node) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_SINK;
	newglob->proxy = (struct pw_proxy*)node;
	pw_node_add_listener(node,&newglob->hook,&node_events,newglob);
	struct pipewire_sink_node *sinknode = calloc(1,sizeof(struct pipewire_sink_node));
	struct wlpavuo_audio_sink *sinkdata = &sinknode->sinkdata;
	newglob->data = sinknode;
	const char *name = spa_dict_lookup(props, "node.description");
	sinkdata->name = name ? strdup(name) : "<no name>";
	// Find the device if we can!
	const char *deviceid = spa_dict_lookup(props, "device.id");
	if (deviceid) {
		uint32_t id = strtoul(deviceid,NULL,10);
		sinknode->device = find_pw_global(id);
		// hack, fix this later!
		sinknode->device->data = newglob;
		sinkdata->data = sinknode;
	}
	wl_list_insert(&pw_state.globals, &newglob->link);
	wl_list_insert(&pw_state.sinks, &sinkdata->link);
}

static void add_pw_client_stream(uint32_t id, const struct spa_dict *props, struct pw_node *node) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM;
	newglob->proxy = (struct pw_proxy*)node;
	pw_node_add_listener(node,&newglob->hook,&node_events,newglob);
	const char *clientidstring = spa_dict_lookup(props,"client.id");
	uint32_t clientid;
	struct pipewire_global *glob = NULL;
	if (clientidstring) {
		clientid = strtoul(clientidstring,NULL,10);
		glob = find_pw_global(clientid);
	}
	struct pipewire_stream_node *streamnode = calloc(1,sizeof(struct pipewire_stream_node));
	struct wlpavuo_audio_client_stream *streamdata = &streamnode->stream;
	streamnode->client = glob;
	if (glob) {
		struct wlpavuo_audio_client *clientdata = glob->data;
		clientdata->streams_count++;
		wl_list_insert(&clientdata->streams, &streamdata->link);
	}
	newglob->data = streamnode;
	streamdata->data = newglob;
	wl_list_insert(&pw_state.globals, &newglob->link);
}

static void add_pw_client(uint32_t id, const struct spa_dict *props, struct pw_client *client) {
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_CLIENT;
	newglob->proxy = (struct pw_proxy*)client;
	struct wlpavuo_audio_client *clientdata = calloc(1,sizeof(struct wlpavuo_audio_client));
	newglob->data = clientdata;
	const char *name = spa_dict_lookup(props, "application.name");
	clientdata->name = name ? strdup(name) : "<no name>";
	wl_list_init(&clientdata->streams);
	wl_list_insert(&pw_state.globals, &newglob->link);
	wl_list_insert(&pw_state.clients, &clientdata->link);
}

static void add_pw_device(uint32_t id, const struct spa_dict *props, struct pw_device *device) {
	UNUSED(props);
	struct pipewire_global *newglob = calloc(1,sizeof(struct pipewire_global));
	newglob->proxy = (struct pw_proxy*)device;
	pw_device_add_listener(device,&newglob->hook,&device_events,newglob);
	newglob->id = id;
	newglob->type = PIPEWIRE_GLOBAL_TYPE_DEVICE;
	wl_list_insert(&pw_state.globals, &newglob->link);
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
				struct pw_node *node = pw_registry_bind(pw_state.registry,id,type,version,0);
				add_pw_sink(id,props,node);
				fire_pipewire_callback();
				return;
			}
			if (strcmp(class,"Stream/Output/Audio") == 0) {
				struct pw_node *node = pw_registry_bind(pw_state.registry,id,type,version,0);
				add_pw_client_stream(id,props,node);
				fire_pipewire_callback();
				return;
			}
		}
	} else if (strcmp(type,PW_TYPE_INTERFACE_Client) == 0) {
		struct pw_client *client = pw_registry_bind(pw_state.registry,id,type,version,0);
		add_pw_client(id,props,client);
		fire_pipewire_callback();
		return;
	} else if (strcmp(type,PW_TYPE_INTERFACE_Device) == 0) {
		struct pw_device *device = pw_registry_bind(pw_state.registry,id,type,version,0);
		add_pw_device(id,props,device);
		fire_pipewire_callback();
		return;
	}
}

static void destroy_global(struct pipewire_global *global) {
	wl_list_remove(&global->link);
	pw_proxy_destroy(global->proxy);
	switch(global->type) {
		case PIPEWIRE_GLOBAL_TYPE_DEVICE:
			break;
		case PIPEWIRE_GLOBAL_TYPE_SINK: {
			struct pipewire_sink_node *sink = global->data;
			wl_list_remove(&sink->sinkdata.link);
			free(sink->sinkdata.name);
			if (sink->r_name != NULL) {
				free(sink->r_name);
			}
			free(sink);
			break;
		}
		case PIPEWIRE_GLOBAL_TYPE_CLIENT_STREAM: {
			struct pipewire_stream_node *stream = global->data;
			wl_list_remove(&stream->stream.link);
			struct pipewire_global *client = stream->client;
			if (client != NULL) {
				struct wlpavuo_audio_client *c = client->data;
				c->streams_count--;
			}
			free(stream->stream.name);
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
	free(global);
}

static void reg_event_global_rm(void *data, uint32_t id) {
	UNUSED(data);
	struct pipewire_global *global;
	wl_list_for_each(global, &pw_state.globals, link) {
		if (global->id == id) {
			destroy_global(global);
			fire_pipewire_callback();
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
	struct pipewire_global *glob;
	struct pipewire_global *globb;
	wl_list_for_each_safe(glob, globb, &pw_state.globals, link) {
		destroy_global(glob);
	}
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
	char buf[1024];
	float pwvols[SPA_AUDIO_MAX_CHANNELS];
	// This is stupid why do I do this like this
	pwvols[0] = pa_sw_volume_to_linear(vol);
	for (unsigned int i = 1; i < SPA_AUDIO_MAX_CHANNELS;i++) {
		pwvols[i] = pwvols[0];
	}
	// I do this like this because clang bitches about not initializing _padding if I use SPA_POD_BUILDER_INIT
	struct spa_pod_builder b = { 0 };
	b.data = buf;
	b.size = sizeof(buf);
	struct spa_pod_frame f;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	spa_pod_builder_prop(&b,SPA_PROP_channelVolumes,0);
	spa_pod_builder_array(&b,sizeof(float),SPA_TYPE_Float,SPA_AUDIO_MAX_CHANNELS,pwvols);
	struct spa_pod *param = spa_pod_builder_pop(&b, &f);
	struct pipewire_global *global = stream->data;
	pw_node_set_param((struct pw_node*)global->proxy,SPA_PARAM_Props,0,param);
}

static void pipewire_set_stream_mute(struct wlpavuo_audio_client_stream *stream, char mute) {
	char buf[1024];
	struct spa_pod_builder b = { 0 };
	b.data = buf;
	b.size = sizeof(buf);
	struct spa_pod_frame f;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
	spa_pod_builder_prop(&b,SPA_PROP_mute,0);
	spa_pod_builder_bool(&b, mute == 1);
	struct spa_pod *param = spa_pod_builder_pop(&b, &f);
	struct pipewire_global *global = stream->data;
	pw_node_set_param((struct pw_node*)global->proxy,SPA_PARAM_Props,0,param);
}

static void build_route_pod(struct spa_pod_builder *b, struct pipewire_sink_node *snode) {
	spa_pod_builder_prop(b,SPA_PARAM_ROUTE_index,0);
	spa_pod_builder_int(b, snode->r_index);
	spa_pod_builder_prop(b,SPA_PARAM_ROUTE_device,0);
	spa_pod_builder_int(b, snode->r_device);
	spa_pod_builder_prop(b,SPA_PARAM_ROUTE_direction,0);
	spa_pod_builder_id(b, snode->r_direction);
	spa_pod_builder_prop(b,SPA_PARAM_ROUTE_name,0);
	spa_pod_builder_string(b, snode->r_name);
}

static void pipewire_set_sink_volume(struct wlpavuo_audio_sink *sink, uint32_t vol) {
	struct pipewire_sink_node *snode = sink->data;
	char buf[1024];
	float pwvols[SPA_AUDIO_MAX_CHANNELS];
	pwvols[0] = pa_sw_volume_to_linear(vol);
	for (unsigned int i = 1; i < SPA_AUDIO_MAX_CHANNELS;i++) {
		pwvols[i] = pwvols[0];
	}
	struct spa_pod_builder b = { 0 };
	b.data = buf;
	b.size = sizeof(buf);
	struct spa_pod_frame f;
	struct spa_pod_frame fin;
	// Seems like unlike with streams, this needs the full params...
	// ... why?
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
	build_route_pod(&b, snode);
	spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props,0);
	spa_pod_builder_push_object(&b, &fin, SPA_TYPE_OBJECT_Props, SPA_PARAM_ROUTE_props);
	spa_pod_builder_prop(&b,SPA_PROP_channelVolumes,0);
	spa_pod_builder_array(&b,sizeof(float),SPA_TYPE_Float,snode->r_channels,pwvols);
	struct spa_pod *param = spa_pod_builder_pop(&b, &fin);
	param = spa_pod_builder_pop(&b, &f);
	pw_device_set_param((struct pw_device*)snode->device->proxy,SPA_PARAM_Route,0,param);
}

static void pipewire_set_sink_mute(struct wlpavuo_audio_sink *sink, char mute) {
	struct pipewire_sink_node *snode = sink->data;
	char buf[1024];
	struct spa_pod_builder b = { 0 };
	b.data = buf;
	b.size = sizeof(buf);
	struct spa_pod_frame f;
	struct spa_pod_frame fin;
	spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);
	build_route_pod(&b, snode);
	spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props,0);
	spa_pod_builder_push_object(&b, &fin, SPA_TYPE_OBJECT_Props, SPA_PARAM_ROUTE_props);
	spa_pod_builder_prop(&b,SPA_PROP_mute,0);
	spa_pod_builder_bool(&b, mute == 1);
	struct spa_pod *param = spa_pod_builder_pop(&b, &fin);
	param = spa_pod_builder_pop(&b, &f);
	pw_device_set_param((struct pw_device*)snode->device->proxy,SPA_PARAM_Route,0,param);
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

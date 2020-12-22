#define _POSIX_C_SOURCE 200809L
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
#include "audio.h"
#define UNUSED(x) (void)(x)

struct wlpavuo_pulse_state {
	pa_threaded_mainloop *mainloop;
	pa_context *context;
	enum wlpavuo_audio_status status;
	wlpavuo_audio_update_cb_t update_callback;
	void *update_callback_data;

	struct wl_list clients; // wlpavuo_pulse_client
	struct wl_list sinks; // wlpavuo_pulse_sink
};
static struct wlpavuo_pulse_state pulse_state = {0};

static void fire_pulse_callback() {
	pulse_state.update_callback(pulse_state.update_callback_data);
}

// All this stuff is pretty inefficient to be honest, but this is relatively simple so shouldn't matter that much..
// It's also a mess! Why does this even work?!

static struct wlpavuo_audio_sink* get_pulse_sink(uint32_t id) {
	struct wlpavuo_audio_sink *sink;
	wl_list_for_each(sink, &pulse_state.sinks, link) {
		if (sink->id == id)
			return sink;
	}
	return NULL;
}

static struct wlpavuo_audio_client* get_pulse_client(uint32_t id) {
	struct wlpavuo_audio_client *client;
	wl_list_for_each(client, &pulse_state.clients, link) {
		if (client->id == id)
			return client;
	}
	return NULL;
}

static struct wlpavuo_audio_client_stream* get_pulse_client_stream(struct wlpavuo_audio_client *client, uint32_t id) {
	struct wlpavuo_audio_client_stream *stream;
	wl_list_for_each(stream, &client->streams, link) {
		if (stream->id == id)
			return stream;
	}
	return NULL;
}


static void handle_sink_input_info(pa_context *c, const pa_sink_input_info *i, int eol, void *userdata) {
	UNUSED(c);
	UNUSED(userdata);
	if (!eol) {
		struct wlpavuo_audio_client *client = get_pulse_client(i->client);
		if (!client) {
			printf("couldn't find client for stream %s\n",i->name);
			return;
		}
		struct wlpavuo_audio_client_stream *stream = get_pulse_client_stream(client,i->index);
		if (!stream) {
			stream = calloc(1,sizeof(struct wlpavuo_audio_client_stream));
			stream->id = i->index;
			if (i->name) {
				stream->name = strdup(i->name);
			} else {
				stream->name = strdup("<unnamed stream>");
			}
			client->streams_count++;
			wl_list_insert(&client->streams, &stream->link);
		}
		stream->volume = i->volume.values[0];
		stream->channels = i->volume.channels;
		stream->flags = i->mute ? WLPAVUO_AUDIO_MUTED : 0;
	} else {
		fire_pulse_callback();
	}
}

static void handle_client_info(pa_context *c, const pa_client_info *i, int eol, void *userdata) {
	UNUSED(c);
	UNUSED(userdata);
	if (!eol) {
		struct wlpavuo_audio_client *client = get_pulse_client(i->index);
		if (!client) {
			client = calloc(1,sizeof(struct wlpavuo_audio_client));
			client->id = i->index;
			client->name = strdup(i->name);
			wl_list_init(&client->streams);
			wl_list_insert(&pulse_state.clients, &client->link);
		}
	} else {
		fire_pulse_callback();
	}
}

static void handle_sink_info(pa_context *c, const pa_sink_info *i, int eol, void *userdata) {
	UNUSED(c);
	UNUSED(userdata);
	if (!eol) {
		struct wlpavuo_audio_sink *sink = get_pulse_sink(i->index);
		if (!sink) {
			sink = calloc(1,sizeof(struct wlpavuo_audio_sink));
			sink->id = i->index;
			sink->name = strdup(i->description);
			wl_list_insert(&pulse_state.sinks, &sink->link);
		}
		sink->channels = i->volume.channels;
		sink->volume = i->volume.values[0];
		sink->flags = i->mute ? WLPAVUO_AUDIO_MUTED : 0;
	} else {
		fire_pulse_callback();
	}
}

static void handle_pulse_event_success(pa_context *c, int success, void *userdata) {
	UNUSED(c);
	UNUSED(success);
	UNUSED(userdata);
}

static void destroy_client_stream(struct wlpavuo_audio_client_stream *stream) {
	free(stream->name);
	free(stream);
}

static void remove_client_stream(uint32_t idx) {
	// Unfortunately we don't know the client, so have to do this the slow way..
	struct wlpavuo_audio_client *client;
	wl_list_for_each(client, &pulse_state.clients, link) {
		struct wlpavuo_audio_client_stream *stream;
		struct wlpavuo_audio_client_stream *streamtmp;
		wl_list_for_each_safe(stream, streamtmp, &client->streams, link) {
			if (stream->id == idx) {
				wl_list_remove(&stream->link);
				destroy_client_stream(stream);
				client->streams_count--;
				return;
			}
		}
	}
}

static void destroy_client(struct wlpavuo_audio_client *client) {
	struct wlpavuo_audio_client_stream *stream;
	struct wlpavuo_audio_client_stream *streamtmp;
	wl_list_for_each_safe(stream, streamtmp, &client->streams, link) {
		wl_list_remove(&stream->link);
		destroy_client_stream(stream);
	}
	free(client->name);
	free(client);
}

static void remove_client(uint32_t idx) {
	struct wlpavuo_audio_client *client;
	wl_list_for_each(client, &pulse_state.clients, link) {
		if (client->id == idx) {
			wl_list_remove(&client->link);
			destroy_client(client);
			return;
		}
	}
}

static void remove_sink(uint32_t idx) {
	struct wlpavuo_audio_client *sink;
	wl_list_for_each(sink, &pulse_state.sinks, link) {
		if (sink->id == idx) {
			wl_list_remove(&sink->link);
			free(sink->name);
			free(sink);
			return;
		}
	}
}


static void handle_pulse_event(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata) {
	UNUSED(userdata);
	pa_subscription_event_type_t facilitymasked = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	pa_subscription_event_type_t typemasked = t & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
	if (facilitymasked == PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
		if (typemasked == PA_SUBSCRIPTION_EVENT_NEW) {
			pa_operation_unref(pa_context_get_sink_input_info(c, idx, handle_sink_input_info, NULL));
		} else if (typemasked == PA_SUBSCRIPTION_EVENT_REMOVE) {
			remove_client_stream(idx);
			fire_pulse_callback();
		} else if (typemasked == PA_SUBSCRIPTION_EVENT_CHANGE) {
			pa_operation_unref(pa_context_get_sink_input_info(c, idx, handle_sink_input_info, NULL));
		}
	} else if (facilitymasked == PA_SUBSCRIPTION_EVENT_CLIENT) {
		if (typemasked == PA_SUBSCRIPTION_EVENT_NEW) {
			pa_operation_unref(pa_context_get_client_info(c, idx, handle_client_info, NULL));
		} else if (typemasked == PA_SUBSCRIPTION_EVENT_REMOVE) {
			remove_client(idx);
			fire_pulse_callback();
		}
	} else if (facilitymasked == PA_SUBSCRIPTION_EVENT_SINK) {
		if (typemasked == PA_SUBSCRIPTION_EVENT_NEW) {
			pa_operation_unref(pa_context_get_sink_info_by_index(c, idx, handle_sink_info, NULL));
		} else if (typemasked == PA_SUBSCRIPTION_EVENT_REMOVE) {
			remove_sink(idx);
			fire_pulse_callback();
		} else if (typemasked == PA_SUBSCRIPTION_EVENT_CHANGE) {
			pa_operation_unref(pa_context_get_sink_info_by_index(c, idx, handle_sink_info, NULL));
		}
	}
}

static void handle_pulse_state(pa_context *c, void *userdata) {
	UNUSED(userdata);
	pa_context_state_t state = pa_context_get_state(c);
	switch (state) {
		case PA_CONTEXT_CONNECTING:
		case PA_CONTEXT_AUTHORIZING:
		case PA_CONTEXT_SETTING_NAME:
		pulse_state.status = WLPAVUO_AUDIO_STATUS_CONNECTING;
		break;
		case PA_CONTEXT_READY:
		pulse_state.status = WLPAVUO_AUDIO_STATUS_READY;
		pa_context_subscribe(pulse_state.context, PA_SUBSCRIPTION_MASK_ALL, handle_pulse_event_success, NULL);
		pa_context_get_sink_info_list(pulse_state.context, handle_sink_info, NULL);
		pa_context_get_client_info_list(c, handle_client_info, NULL);
		pa_operation_unref(pa_context_get_sink_input_info_list(c, handle_sink_input_info, NULL));
		break;
		default:
		pulse_state.status = WLPAVUO_AUDIO_STATUS_FAILED;
		break;
	}
}

void wlpavuo_pulse_set_stream_mute(struct wlpavuo_audio_client_stream *stream, char muted) {
	pa_operation_unref(pa_context_set_sink_input_mute(pulse_state.context, stream->id, muted, handle_pulse_event_success,NULL));
}
void wlpavuo_pulse_set_sink_mute(struct wlpavuo_audio_sink *sink, char muted) {
	pa_operation_unref(pa_context_set_sink_mute_by_index(pulse_state.context, sink->id, muted, handle_pulse_event_success,NULL));
}

void wlpavuo_pulse_set_stream_volume(struct wlpavuo_audio_client_stream *stream, uint32_t volume) {
	struct pa_cvolume vol;
	for (int i = 0; i < stream->channels;i++) {
		vol.values[i] = volume;
	}
	vol.channels = stream->channels;
	pa_operation_unref(pa_context_set_sink_input_volume(pulse_state.context, stream->id, &vol, handle_pulse_event_success,NULL));
}

void wlpavuo_pulse_set_sink_volume(struct wlpavuo_audio_sink *sink, uint32_t volume) {
	struct pa_cvolume vol;
	for (int i = 0; i < sink->channels;i++) {
		vol.values[i] = volume;
	}
	vol.channels = sink->channels;
	pa_operation_unref(pa_context_set_sink_volume_by_index(pulse_state.context, sink->id, &vol, handle_pulse_event_success,NULL));
}

enum wlpavuo_audio_status wlpavuo_pulse_init() {
	if (!pulse_state.context && pulse_state.status != WLPAVUO_AUDIO_STATUS_FAILED) {
		wl_list_init(&pulse_state.clients);
		wl_list_init(&pulse_state.sinks);
		pulse_state.mainloop = pa_threaded_mainloop_new();
		pa_proplist *props = pa_proplist_new();
		pa_proplist_setf(props, PA_PROP_APPLICATION_NAME, "WlPaVUOverlay");
		pa_proplist_setf(props, PA_PROP_APPLICATION_ID, "wlpavuoverlay");
		pulse_state.context = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(pulse_state.mainloop), "WlPaVUOverlay",props);
		pa_proplist_free(props);
		pa_context_set_state_callback(pulse_state.context, handle_pulse_state, NULL);
		pa_context_set_subscribe_callback(pulse_state.context, handle_pulse_event, NULL);
		pulse_state.status = WLPAVUO_AUDIO_STATUS_CONNECTING;
		pa_context_connect(pulse_state.context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL);
		pa_threaded_mainloop_start(pulse_state.mainloop);
	}
	return pulse_state.status;
}

struct wl_list *wlpavuo_pulse_get_clients() {
	return &pulse_state.clients;
}
struct wl_list *wlpavuo_pulse_get_sinks() {
	return &pulse_state.sinks;
}

void wlpavuo_pulse_uninit() {
	pa_context_disconnect(pulse_state.context);
	pa_threaded_mainloop_stop(pulse_state.mainloop);
	pa_threaded_mainloop_free(pulse_state.mainloop);

	struct wlpavuo_audio_client *client;
	struct wlpavuo_audio_client *clienttmp;
	wl_list_for_each_safe(client, clienttmp, &pulse_state.clients, link) {
		wl_list_remove(&client->link);
		destroy_client(client);
	}
	struct wlpavuo_audio_client *sink;
	struct wlpavuo_audio_client *sinktmp;
	wl_list_for_each_safe(sink, sinktmp, &pulse_state.sinks, link) {
		wl_list_remove(&sink->link);
		free(sink->name);
		free(sink);
	}
}

void wlpavuo_pulse_set_update_callback(wlpavuo_audio_update_cb_t cb, void *data) {
	pulse_state.update_callback_data = data;
	pulse_state.update_callback = cb;
}

void wlpavuo_pulse_lock() {
	pa_threaded_mainloop_lock(pulse_state.mainloop);
}
void wlpavuo_pulse_unlock() {
	pa_threaded_mainloop_unlock(pulse_state.mainloop);
}

static const char* pulse_get_name() {
	return "PulseAudio";
}

static const struct wlpavuo_audio_impl pulse_impl = {
	pulse_get_name,
	wlpavuo_pulse_init,
	wlpavuo_pulse_uninit,
	wlpavuo_pulse_set_update_callback,
	wlpavuo_pulse_lock,
	wlpavuo_pulse_unlock,
	wlpavuo_pulse_set_stream_volume,
	wlpavuo_pulse_set_stream_mute,
	wlpavuo_pulse_set_sink_volume,
	wlpavuo_pulse_set_sink_mute,
	wlpavuo_pulse_get_clients,
	wlpavuo_pulse_get_sinks
};

const struct wlpavuo_audio_impl* wlpavuo_audio_get_pa() {
	return &pulse_impl;
}

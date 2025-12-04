#include <gst/gst.h>
#include <gst/net/gstnet.h>
#include <string.h>

static const gint CLOCK_PORT = 8557;

typedef struct _AppData {
  GstElement *pipeline;
  GMainLoop *loop;

  GstElement *filesrc;
  GstElement *tsdemux;

  GstElement *video_queue;
  GstElement *video_parse;
  GstElement *video_dec;
  GstElement *video_convert;
  GstElement *video_sink;

  GstElement *audio_queue;
  GstElement *audio_parse;

  GstElement *audio_dec;
  GstElement *audio_convert;
  GstElement *audio_resample;
  GstElement *audio_enc;

  GstElement *rtsp_sink;

  gboolean video_linked;
  gboolean audio_linked;

  GstClock *clock;
  GstNetTimeProvider *net_time_provider;  
  GstClock *net_clock;

} AppData;

/* utility to make element and warn */
static GstElement *MakeElement(const char *factory, const char *name) {
  GstElement *e = gst_element_factory_make(factory, name);
  if (!e)
    g_printerr("Fail to create element %s (%s)\n", name ? name : "(no-name)",
               factory);
  return e;
}

/* Bus watch */
static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = (GMainLoop *)data;
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      g_print("EOS received\n");
      g_main_loop_quit(loop);
      break;
    case GST_MESSAGE_ERROR: {
      GError *err = NULL;
      gchar *dbg = NULL;
      gst_message_parse_error(msg, &err, &dbg);
      g_printerr("Error: %s\nDebug: %s\n", err->message, dbg ? dbg : "NULL");
      g_error_free(err);
      g_free(dbg);
      g_main_loop_quit(loop);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data) {
  AppData *app = (AppData *)data;
  GstCaps *caps;
  GstStructure *structure;
  const gchar *name;
  GstPad *sink_pad = NULL;

  caps = gst_pad_get_current_caps(pad);
  structure = gst_caps_get_structure(caps, 0);
  name = gst_structure_get_name(structure);

  if (g_str_has_prefix(name, "video/x-h264") && !app->video_linked) {
    sink_pad = gst_element_get_static_pad(app->video_queue, "sink");
    if (!gst_pad_is_linked(sink_pad)) {
      if (gst_pad_link(pad, sink_pad) == GST_PAD_LINK_OK) {
        g_print("<> Video connected\n");
        app->video_linked = TRUE;
      }
    }
    gst_object_unref(sink_pad);
  } else if (g_str_has_prefix(name, "audio/mpeg") && !app->audio_linked) {
    sink_pad = gst_element_get_static_pad(app->audio_queue, "sink");
    if (!gst_pad_is_linked(sink_pad)) {
      const gchar *stream_format =
          gst_structure_get_string(structure, "stream-format");
      if (gst_pad_link(pad, sink_pad) == GST_PAD_LINK_OK) {
        g_print("<> Audio connected (format: %s)\n",
                stream_format ? stream_format : "unknown");
        app->audio_linked = TRUE;
      }
    }
    gst_object_unref(sink_pad);
  }

  gst_caps_unref(caps);
}

static void on_decoder_pad_added(GstElement *element, GstPad *pad, gpointer data) {
  
  AppData *app = (AppData *)data;
  GstCaps *caps;
  GstStructure *structure;
  const gchar *name;
  GstPad *sink_pad = NULL;

  caps = gst_pad_get_current_caps(pad);
  structure = gst_caps_get_structure(caps, 0);
  name = gst_structure_get_name(structure);

  if (g_str_has_prefix(name, "audio/x-raw")) {
    sink_pad = gst_element_get_static_pad(app->audio_convert, "sink");
    if (!gst_pad_is_linked(sink_pad)) {
      if (gst_pad_link(pad, sink_pad) == GST_PAD_LINK_OK) {
        g_print("[] Audio Decoder linked to Encoder\n");
      }
    }
    gst_object_unref(sink_pad);
  }
}

static gboolean setup_pipeline(AppData *app, gchar *path,
                               const char *rtsp_url) {
  app->pipeline = gst_pipeline_new("mpegts-pipeline");
  if (!app->pipeline) {
    g_printerr("Fail to create pipeline.\n");
    return FALSE;
  }

  app->filesrc = MakeElement("filesrc", "file-source");
  app->tsdemux = MakeElement("tsdemux", "ts-demux");

  app->video_queue = MakeElement("queue", "video-queue");
  app->video_parse = MakeElement("h264parse", "video-parse");
  app->video_dec = MakeElement("avdec_h264", "video-decoder");
  app->video_convert = MakeElement("videoconvert", "video-convert");
  app->video_sink = MakeElement("autovideosink", "video-sink");

  app->audio_queue = MakeElement("queue", "audio-queue");
  app->audio_parse = MakeElement("aacparse", "audio-parse");

  app->audio_dec = MakeElement("decodebin", "audio-dec");
  app->audio_convert = MakeElement("audioconvert", "audio-convert");
  app->audio_resample = MakeElement("audioresample", "audio-resample");
  app->audio_enc = MakeElement("opusenc", "audio-enc");

  app->rtsp_sink = MakeElement("rtspclientsink", "rtsp-sink");

  if (!app->filesrc || !app->tsdemux || !app->tsdemux || !app->video_queue || !app->video_parse || !app->video_dec ||
      !app->video_convert || !app->video_sink || !app->audio_queue || 
      !app->audio_parse || !app->audio_dec || !app->audio_convert || !app->audio_resample || !app->audio_enc || !app->rtsp_sink) {
    g_printerr("All esssential elements must be created\n");
    return FALSE;
  }

  g_object_set(app->filesrc, "location", path, NULL);

  g_object_set(app->rtsp_sink, "location", rtsp_url, "ntp-time-source", 0, NULL);

  g_object_set(app->video_sink, "ts-offset", 200 * GST_MSECOND, NULL);
  g_object_set(app->video_sink, "sync", TRUE, NULL);

  gst_bin_add_many(GST_BIN(app->pipeline), app->filesrc, app->tsdemux,
                   app->video_queue, app->video_parse, app->video_dec, app->video_convert, app->video_sink,
                   app->audio_queue, app->audio_dec, app->audio_convert, app->audio_resample, app->audio_enc,
                   app->rtsp_sink, 
                   NULL);

  gst_element_link(app->filesrc, app->tsdemux);

  if (!gst_element_link_many(app->video_queue, app->video_parse, app->video_dec, app->video_convert,
                             app->video_sink, NULL)) {
    g_printerr("Fail connecting Video branch\n");
    return FALSE;
  }

  if (!gst_element_link_many(app->audio_queue, app->audio_dec, NULL)) {
    g_printerr("Fail connecting Audio branch\n");
    return FALSE;
  }
  
  GstCaps *caps = gst_caps_new_simple("audio/x-opus", NULL);
  if (!gst_element_link_filtered(app->audio_enc, app->rtsp_sink, caps)) {
    g_printerr("Fail connecting Audio branch\n");
    return FALSE;
  }

  if (!gst_element_link_many(app->audio_convert, app->audio_resample, app->audio_enc, NULL)) {
    g_printerr("Fail connecting Audio branch\n");
    return FALSE;
  }
  
  g_signal_connect(app->tsdemux, "pad-added", G_CALLBACK(on_pad_added), app);
  g_signal_connect(app->audio_dec, "pad-added", G_CALLBACK(on_decoder_pad_added), app);

  return TRUE;
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);

  if (argc < 2) {
    g_printerr("Usage: %s file.ts\n", argv[0]);
    return -1;
  }

  AppData app;
  memset(&app, 0, sizeof(app));

  app.loop = g_main_loop_new(NULL, FALSE);

  if (!setup_pipeline(&app, argv[1], argv[2])) {
    g_printerr("Failed to setup pipeline\n");
    return -1;
  }

  /* Bus */
  GstBus *bus = gst_element_get_bus(app.pipeline);
  gst_bus_add_watch(bus, bus_call, app.loop);
  gst_object_unref(bus);


  
  app.clock = gst_system_clock_obtain();
  if (!app.clock) {
    g_printerr("Failed to get pipeline clock\n");
    return -1;
  }

  app.net_time_provider = gst_net_time_provider_new(app.clock, NULL, CLOCK_PORT);
  if (!app.net_time_provider) {
    g_printerr("Failed to create GstNetTimeProvider\n");
    return -1;
  }

  app.net_clock = gst_net_client_clock_new("net_clock", "127.0.0.1", CLOCK_PORT, 0);

  if (!app.net_clock) {
    g_printerr("Failed to create network clock\n");
    return -1;
  }
  // Wait synchronization
  g_print("Waiting for clock synchronization ...\n");
  if (!gst_clock_wait_for_sync(app.net_clock, 5 * GST_SECOND)) {
    g_printerr("Warning: Clock sync timeout\n");
  } else {
    g_print("Sync OK!\n");
  }

  // Force pipeline to use SystemClock
  gst_pipeline_use_clock(GST_PIPELINE(app.pipeline), app.net_clock);
  

  g_print("Iniciando pipeline...\n");
  gst_element_set_state(app.pipeline, GST_STATE_PLAYING);
  
  // Wait pipeline estabilization
  GstStateChangeReturn ret = gst_element_get_state(app.pipeline, NULL, NULL, 5 * GST_SECOND);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Failed to start pipeline\n");
    return -1;
  }

  g_print("Clock provider rodando na porta 8557\n");
  g_print("Clientes devem usar: clock-address=127.0.0.1 clock-port=8557\n\n");
  g_print("Pipeline em PLAY – lendo: %s\n", argv[1]);
  g_print("Publicando áudio em: %s\n", argv[2]);  


  g_main_loop_run(app.loop);

  /* Cleanup */
  if (app.net_time_provider) {
    g_object_unref(app.net_time_provider);
  }
  if (app.clock) {
    gst_object_unref(app.clock);
  }
  gst_element_set_state(app.pipeline, GST_STATE_NULL);
  gst_object_unref(app.pipeline);
  g_main_loop_unref(app.loop);

  return 0;
}

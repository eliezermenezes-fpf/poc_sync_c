#include <gst/gst.h>
#include <gst/net/gstnet.h>

typedef struct _ClientData {
  GstElement *playbin;
  GstClock *net_clock;
  GMainLoop *loop;
} ClientData;

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
  GMainLoop *loop = ((ClientData*)data)->loop;
  
  switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_EOS:
      g_print("End of stream\n");
      g_main_loop_quit(loop);
      break;
      
    case GST_MESSAGE_ERROR: {
      gchar *debug;
      GError *error;
      gst_message_parse_error(msg, &error, &debug);
      g_printerr("Error: %s\n", error->message);
      if (debug) g_printerr("Debug: %s\n", debug);
      g_error_free(error);
      g_free(debug);
      g_main_loop_quit(loop);
      break;
    }
    
    case GST_MESSAGE_ELEMENT: {
      const GstStructure *s = gst_message_get_structure(msg);
      const gchar *name = gst_structure_get_name(s);
      
      // Monitorar informações de sincronização RTP
      if (g_str_has_prefix(name, "GstRTCPPacket")) {
        g_print("RTCP sync info received\n");
      }
      break;
    }
    
    default:
      break;
  }
  
  return TRUE;
}

static void on_source_setup(GstElement *playbin, GstElement *source, gpointer data) {
  if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "ntp-sync")) {
    g_print("Enabling NTP sync into rtspsrc\n");
    g_object_set(source, "ntp-sync", TRUE, NULL);
    g_object_set(source, "buffer-mode", 4, NULL); // 4 = synced
    g_object_set(source, "ntp-time-source", 0, NULL); // 3 = NTP
    g_object_set(source, "latency", 200, NULL); // 3 = NTP
  }
}

int main(int argc, char *argv[]) {
  ClientData client;
  GstBus *bus;
  
  gst_init(&argc, &argv);
  
  if (argc < 4) {
    g_printerr("Usage: %s <rtsp_url> <clock_ip> <clock_port>\n", argv[0]);
    g_printerr("Example: %s rtsp://127.0.0.1:8554/audio 127.0.0.1 8555\n", argv[0]);
    return -1;
  }
  
  memset(&client, 0, sizeof(client));
  client.loop = g_main_loop_new(NULL, FALSE);
  
  client.playbin = gst_element_factory_make("playbin3", "player");
  if (!client.playbin) {
    client.playbin = gst_element_factory_make("playbin", "player");
    if (!client.playbin) {
      g_printerr("Failed to create playbin\n");
      return -1;
    }
  }
  
  g_object_set(client.playbin, "uri", argv[1], NULL);
  
  g_signal_connect(client.playbin, "source-setup", G_CALLBACK(on_source_setup), &client);
  
  gint flags;
  g_object_get(client.playbin, "flags", &flags, NULL);
  flags &= ~0x00000001; // Disable video
  flags |= 0x00000002;  // Enable audio
  g_object_set(client.playbin, "flags", flags, NULL);
  
  // Config net clock
  gint clock_port = atoi(argv[3]);
  g_print("Connected to clock -> %s:%d\n", argv[2], clock_port);
  
  client.net_clock = gst_net_client_clock_new("net_clock", argv[2], clock_port, 0);
  
  if (!client.net_clock) {
    g_printerr("Failed to create network clock\n");
    return -1;
  }
  
  // Wait synchronization
  g_print("Waiting clock synchronization ...\n");
  if (!gst_clock_wait_for_sync(client.net_clock, 5 * GST_SECOND)) {
    g_printerr("Warning: Clock sync timeout\n");
  } else {
    g_print("Clock synchronized!\n");
  }
  
  gst_pipeline_use_clock(GST_PIPELINE(client.playbin), client.net_clock);
  
  // Bus
  bus = gst_element_get_bus(client.playbin);
  gst_bus_add_watch(bus, bus_call, &client);
  gst_object_unref(bus);
  
  // Start
  g_print("\nStarting playback using NTP-RTCP\n\n");
  
  if (gst_element_set_state(client.playbin, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    g_printerr("Failed to set playbin to PLAYING state\n");
    return -1;
  }
  
  g_main_loop_run(client.loop);
  
  // Cleanup
  g_print("\nFinalizing...\n");
  gst_element_set_state(client.playbin, GST_STATE_NULL);
  gst_object_unref(client.playbin);
  if (client.net_clock) {
    gst_object_unref(client.net_clock);
  }
  g_main_loop_unref(client.loop);
  
  return 0;
}
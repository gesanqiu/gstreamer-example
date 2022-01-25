# Playback tutorial 3: Short-cutting the pipeline

## Goal

在[Basic tutorial 8: Short-cutting the pipeline]()中展示了一个应用程序如何通过`appsink`和`appsrc`插件手动地从pipeline中提取和插入数据。`playbin`同样允许使用这两个插件，但是连接的方式不一样。要将`playbin`与`appsink`连接请阅读[Playback tutorial 7: Custom playbin sinks]()。这篇教程展示了：

- 如何将`appsrc`与`playbin`连接。
- 如何配置appsrc。

## A playbin waveform generator

```c++
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>

#define CHUNK_SIZE 1024   /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 44100 /* Samples per second we are sending */

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *app_source;

  guint64 num_samples;   /* Number of samples generated so far (for timestamp generation) */
  gfloat a, b, c, d;     /* For waveform generation */

  guint sourceid;        /* To control the GSource */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;

/* This method is called by the idle GSource in the mainloop, to feed CHUNK_SIZE bytes into appsrc.
 * The ide handler is added to the mainloop when appsrc requests us to start sending data (need-data signal)
 * and is removed when appsrc has enough data (enough-data signal).
 */
static gboolean push_data (CustomData *data) {
  GstBuffer *buffer;
  GstFlowReturn ret;
  int i;
  GstMapInfo map;
  gint16 *raw;
  gint num_samples = CHUNK_SIZE / 2; /* Because each sample is 16 bits */
  gfloat freq;

  /* Create a new empty buffer */
  buffer = gst_buffer_new_and_alloc (CHUNK_SIZE);

  /* Set its timestamp and duration */
  GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (data->num_samples, GST_SECOND, SAMPLE_RATE);
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (num_samples, GST_SECOND, SAMPLE_RATE);

  /* Generate some psychodelic waveforms */
  gst_buffer_map (buffer, &map, GST_MAP_WRITE);
  raw = (gint16 *)map.data;
  data->c += data->d;
  data->d -= data->c / 1000;
  freq = 1100 + 1000 * data->d;
  for (i = 0; i < num_samples; i++) {
    data->a += data->b;
    data->b -= data->a / freq;
    raw[i] = (gint16)(500 * data->a);
  }
  gst_buffer_unmap (buffer, &map);
  data->num_samples += num_samples;

  /* Push the buffer into the appsrc */
  g_signal_emit_by_name (data->app_source, "push-buffer", buffer, &ret);

  /* Free the buffer now that we are done with it */
  gst_buffer_unref (buffer);

  if (ret != GST_FLOW_OK) {
    /* We got some error, stop sending data */
    return FALSE;
  }

  return TRUE;
}

/* This signal callback triggers when appsrc needs data. Here, we add an idle handler
 * to the mainloop to start pushing data into the appsrc */
static void start_feed (GstElement *source, guint size, CustomData *data) {
  if (data->sourceid == 0) {
    g_print ("Start feeding\n");
    data->sourceid = g_idle_add ((GSourceFunc) push_data, data);
  }
}

/* This callback triggers when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
static void stop_feed (GstElement *source, CustomData *data) {
  if (data->sourceid != 0) {
    g_print ("Stop feeding\n");
    g_source_remove (data->sourceid);
    data->sourceid = 0;
  }
}

/* This function is called when an error message is posted on the bus */
static void error_cb (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;

  /* Print error details on the screen */
  gst_message_parse_error (msg, &err, &debug_info);
  g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
  g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
  g_clear_error (&err);
  g_free (debug_info);

  g_main_loop_quit (data->main_loop);
}

/* This function is called when playbin has created the appsrc element, so we have
 * a chance to configure it. */
static void source_setup (GstElement *pipeline, GstElement *source, CustomData *data) {
  GstAudioInfo info;
  GstCaps *audio_caps;

  g_print ("Source has been created. Configuring.\n");
  data->app_source = source;

  /* Configure appsrc */
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
  audio_caps = gst_audio_info_to_caps (&info);
  g_object_set (source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (source, "need-data", G_CALLBACK (start_feed), data);
  g_signal_connect (source, "enough-data", G_CALLBACK (stop_feed), data);
  gst_caps_unref (audio_caps);
}

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;

  /* Initialize cumstom data structure */
  memset (&data, 0, sizeof (data));
  data.b = 1; /* For waveform generation */
  data.d = 1;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the playbin element */
  data.pipeline = gst_parse_launch ("playbin uri=appsrc://", NULL);
  g_signal_connect (data.pipeline, "source-setup", G_CALLBACK (source_setup), &data);

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data.pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, &data);
  gst_object_unref (bus);

  /* Start playing the pipeline */
  gst_element_set_state (data.pipeline, GST_STATE_PLAYING);

  /* Create a GLib Main Loop and set it to run */
  data.main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.main_loop);

  /* Free resources */
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}
```

这个例程将打开一个窗口并播放一个含有音频的电影。这段媒体是从互联网获取的，所以窗口可能需要一定的时间才会出现，这取决于你的网络连接速度。在控制台窗口中，您应该会看到一条消息，指示媒体存储的位置，以及一个表示下载部分和当前位置的文本图。一条缓冲消息将在请求缓冲是打印，但当你的网速足够快时这条消息不会出现。

## Walkthrough

为了使用`appsrc`作为这条pipeline的数据源，实例化一个`playbin`对象并将`uri`属性设置为`appsrc://`。

```c++
/* Create the playbin element */
data.pipeline = gst_parse_launch ("playbin uri=appsrc://", NULL);
```

`playbin`将在内部创建一个`appsrc`元素，并且发出`source-setup`信号以通知应用程序来配置它：

```c++
g_signal_connect (data.pipeline, "source-setup", G_CALLBACK (source_setup), &data);
```

需要注意的是，设置`appsrc`的caps是很重要的，因为一旦信号句柄（`source_setup`回调函数）返回，`playbin`将基于这个caps实例化下一个pipeline的下一个元素，假如caps没有被正确设置会影响整个pipeline的运行（一个常见的现象就是`appsrc`的`need-data`回调可能触发了一次之后就不再触发）：

```c++
/* This function is called when playbin has created the appsrc element, so we have
 * a chance to configure it. */
static void source_setup (GstElement *pipeline, GstElement *source, CustomData *data) {
  GstAudioInfo info;
  GstCaps *audio_caps;

  g_print ("Source has been created. Configuring.\n");
  data->app_source = source;

  /* Configure appsrc */
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
  audio_caps = gst_audio_info_to_caps (&info);
  g_object_set (source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (source, "need-data", G_CALLBACK (start_feed), data);
  g_signal_connect (source, "enough-data", G_CALLBACK (stop_feed), data);
  gst_caps_unref (audio_caps);
}
```

这里关于`appsrc`的配置和[Basic tutorial 8: Short-cutting the pipeline]()中的完全一致：caps被设置为`audio/x-raw`，注册了两个回调函数，因此`appsrc`可以通知应用程序何时开始和停止输送数据。可以阅读[Basic tutorial 8: Short-cutting the pipeline]()以获得更多细节。

除此以外，`playbin`负责pipeline的剩余部分，应用程序只需要负责生成数据。

想知道如何使用`appsink`从`playbin`中提取数据，请阅读[Playback tutorial 7: Custom playbin sinks]()。

## Conclusion

这篇教程在`playbin`上实现了[Basic tutorial 8: Short-cutting the pipeline]()中的操作：

- 如何通过设置`playbin`的`uri`属性为`appsrc://`来连接`appsrc`。
- 如何通过`source-setup`信号配置`appsrc`。


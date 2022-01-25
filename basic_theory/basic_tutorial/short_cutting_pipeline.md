# Basic tutorial 8: Short-cutting the pipeline

## 目标

GStreamer构造的pipeline不需要完全封闭，有几种方式允许用户在任意时间向pipeline注入或提取数据。本教程将展示：

- 如何将外部数据注入通用的GStreamer pipeline。
- 如何从通用的GStreamer pipeline中提取数据。
- 如何访问和操作从GStreamer pipeline中取出的数据。

[Playback tutorial 3: Short-cutting the pipeline]()中使用基于playbin的pipeline以另外一种方式实现了相同的目标。

## 介绍

应用程序可以通过几种方式与流经GStreamer pipeline的数据交互。本教程将展示最简单的一种，因为使用了为这一目的所设计的element：`appsink`和`appsrc`。

### Buffer

数据以称为buffers(缓冲区)的块的形式通过GStreamer管道传输。由于本例生产和消费数据，我们有必要了解GstBuffer。

Source pads生产buffer，而sink pads消费buffer，GStreamer接受这些buffer并将它们从一个element传递到另一个element。

一个buffer仅代表一个数据单元，用户不应该假设：

- 所有的buffers拥有相同的大小
- 一个buffer进入一个element就会有一个buffer从这个element出来

Elements可以随意处理它们接收到的buffer。

GstBuffer可能包含不止一个世纪的内存缓冲区，实际的内存缓冲区是使用GstMemory对象抽象出来的，一个GstBuffer可以包含多个GstMemory对象。

每个buffer都附有时间戳和持续时间，描述了buffer内容应该被解码、渲染或播放的时刻。事实上时间戳是一个非常复杂而微妙的主题但目前这个简单的解释已经足够了。

举例来说，一个`filesrc`(可以读取文件的GStreamer element)插件生产的buffer具有`ANY`类型的caps和无时间戳信息。而经过解复用(详见[Basic tutorial 3: Dynamic pipelines](https://ricardolu.gitbook.io/gstreamer/basic-theory/basic-tutorial-3-dynamic-pipelines))之后buffer将拥有一些特殊的caps，例如`video/x-h264`。在经过解码之后，每一个buffer都将含有一帧具有原始caps的视频帧，例如`video/x-raw-yuv`以及非常准确的时间戳，标记了这一阵将被播放的时间。

## 教程

本教程是[Basic tutorial 7: Multithreading and Pad Availability]()的拓展，主要包含两个方面：

- `audiotesetsrc`被`appsrc`取代，音频数据将由`appsrc`产生。
- `tee`插件增加了一个分支，因此流入audio sink和波形显示的数据也被复制了一份传给`appsink`。

`appsink`会将信息回传到应用程序中，在本教程中仅仅是通知用户收到了新的数据，但是显然`appsink`可以处理更复杂的任务。

![img](images/short_cutting_pipeline/basic-tutorial-8.png)

## A crude waveform generator

### basic-tutorial-8.c

```c
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <string.h>

#define CHUNK_SIZE 1024   /* Amount of bytes we are sending in each buffer */
#define SAMPLE_RATE 44100 /* Samples per second we are sending */

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline, *app_source, *tee, *audio_queue, *audio_convert1, *audio_resample, *audio_sink;
  GstElement *video_queue, *audio_convert2, *visual, *video_convert, *video_sink;
  GstElement *app_queue, *app_sink;

  guint64 num_samples;   /* Number of samples generated so far (for timestamp generation) */
  gfloat a, b, c, d;     /* For waveform generation */

  guint sourceid;        /* To control the GSource */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;

/* This method is called by the idle GSource in the mainloop, to feed CHUNK_SIZE bytes into appsrc.
 * The idle handler is added to the mainloop when appsrc requests us to start sending data (need-data signal)
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

/* The appsink has received a buffer */
static GstFlowReturn new_sample (GstElement *sink, CustomData *data) {
  GstSample *sample;

  /* Retrieve the buffer */
  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (sample) {
    /* The only thing we do in this example is print a * to indicate a received buffer */
    g_print ("*");
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }

  return GST_FLOW_ERROR;
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

int main(int argc, char *argv[]) {
  CustomData data;
  GstPad *tee_audio_pad, *tee_video_pad, *tee_app_pad;
  GstPad *queue_audio_pad, *queue_video_pad, *queue_app_pad;
  GstAudioInfo info;
  GstCaps *audio_caps;
  GstBus *bus;

  /* Initialize custom data structure */
  memset (&data, 0, sizeof (data));
  data.b = 1; /* For waveform generation */
  data.d = 1;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.app_source = gst_element_factory_make ("appsrc", "audio_source");
  data.tee = gst_element_factory_make ("tee", "tee");
  data.audio_queue = gst_element_factory_make ("queue", "audio_queue");
  data.audio_convert1 = gst_element_factory_make ("audioconvert", "audio_convert1");
  data.audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
  data.audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
  data.video_queue = gst_element_factory_make ("queue", "video_queue");
  data.audio_convert2 = gst_element_factory_make ("audioconvert", "audio_convert2");
  data.visual = gst_element_factory_make ("wavescope", "visual");
  data.video_convert = gst_element_factory_make ("videoconvert", "video_convert");
  data.video_sink = gst_element_factory_make ("autovideosink", "video_sink");
  data.app_queue = gst_element_factory_make ("queue", "app_queue");
  data.app_sink = gst_element_factory_make ("appsink", "app_sink");

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.app_source || !data.tee || !data.audio_queue || !data.audio_convert1 ||
      !data.audio_resample || !data.audio_sink || !data.video_queue || !data.audio_convert2 || !data.visual ||
      !data.video_convert || !data.video_sink || !data.app_queue || !data.app_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Configure wavescope */
  g_object_set (data.visual, "shader", 0, "style", 0, NULL);

  /* Configure appsrc */
  gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
  audio_caps = gst_audio_info_to_caps (&info);
  g_object_set (data.app_source, "caps", audio_caps, "format", GST_FORMAT_TIME, NULL);
  g_signal_connect (data.app_source, "need-data", G_CALLBACK (start_feed), &data);
  g_signal_connect (data.app_source, "enough-data", G_CALLBACK (stop_feed), &data);

  /* Configure appsink */
  g_object_set (data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);
  g_signal_connect (data.app_sink, "new-sample", G_CALLBACK (new_sample), &data);
  gst_caps_unref (audio_caps);

  /* Link all elements that can be automatically linked because they have "Always" pads */
  gst_bin_add_many (GST_BIN (data.pipeline), data.app_source, data.tee, data.audio_queue, data.audio_convert1, data.audio_resample,
      data.audio_sink, data.video_queue, data.audio_convert2, data.visual, data.video_convert, data.video_sink, data.app_queue,
      data.app_sink, NULL);
  if (gst_element_link_many (data.app_source, data.tee, NULL) != TRUE ||
      gst_element_link_many (data.audio_queue, data.audio_convert1, data.audio_resample, data.audio_sink, NULL) != TRUE ||
      gst_element_link_many (data.video_queue, data.audio_convert2, data.visual, data.video_convert, data.video_sink, NULL) != TRUE ||
      gst_element_link_many (data.app_queue, data.app_sink, NULL) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Manually link the Tee, which has "Request" pads */
  tee_audio_pad = gst_element_request_pad_simple (data.tee, "src_%u");
  g_print ("Obtained request pad %s for audio branch.\n", gst_pad_get_name (tee_audio_pad));
  queue_audio_pad = gst_element_get_static_pad (data.audio_queue, "sink");
  tee_video_pad = gst_element_request_pad_simple (data.tee, "src_%u");
  g_print ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
  queue_video_pad = gst_element_get_static_pad (data.video_queue, "sink");
  tee_app_pad = gst_element_request_pad_simple (data.tee, "src_%u");
  g_print ("Obtained request pad %s for app branch.\n", gst_pad_get_name (tee_app_pad));
  queue_app_pad = gst_element_get_static_pad (data.app_queue, "sink");
  if (gst_pad_link (tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
      gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK ||
      gst_pad_link (tee_app_pad, queue_app_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Tee could not be linked\n");
    gst_object_unref (data.pipeline);
    return -1;
  }
  gst_object_unref (queue_audio_pad);
  gst_object_unref (queue_video_pad);
  gst_object_unref (queue_app_pad);

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

  /* Release the request pads from the Tee, and unref them */
  gst_element_release_request_pad (data.tee, tee_audio_pad);
  gst_element_release_request_pad (data.tee, tee_video_pad);
  gst_element_release_request_pad (data.tee, tee_app_pad);
  gst_object_unref (tee_audio_pad);
  gst_object_unref (tee_video_pad);
  gst_object_unref (tee_app_pad);

  /* Free resources */
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}
```

## 工作流

例程的131-205行创建了一条[Basic tutorial 7: Multithreading and Pad Availability]()中pipeline的拓展版本，包括实例化所有elements，自动连接所有具有`Always Pads`的elements，手动连接从`tee`中申请的`Request Pads`。

### appsrc

```c
/* Configure appsrc */
gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, SAMPLE_RATE, 1, NULL);
audio_caps = gst_audio_info_to_caps (&info);
g_object_set (data.app_source, "caps", audio_caps, NULL);
g_signal_connect (data.app_source, "need-data", G_CALLBACK (start_feed), &data);
g_signal_connect (data.app_source, "enough-data", G_CALLBACK (stop_feed), &data);
```

`appsrc`首要设置的属性就是它的caps，它指定了`appsrc`将生成的数据类型，以便GStreamer可以检查它是否能够和下游elements连接(下游elements能否处理这种数据)。caps属性值必须是GstCaps对象，GstCaps对象可以使用`gst_caps_from_string()`解析一个字符串对象来构建。

我们连接了`appsrc`的`need-data`和`enough-data`信号，它们将在`appsrc`内部队列数据不足或快满时分别被触发。本教程使用这两个信号分别启动/停止信号发生过程。

### appsink

```c
/* Configure appsink */
g_object_set (data.app_sink, "emit-signals", TRUE, "caps", audio_caps, NULL);
g_signal_connect (data.app_sink, "new-sample", G_CALLBACK (new_sample), &data);
gst_caps_unref (audio_caps);
```

我们连接了`appsin`的`new-sample`信号，每当`appsink`到数据的时候就会触发这个信号。与`appsrc`不同，`appsink`的`emit-signals`属性的默认值为`false`，因此我们需要将它设置为`true`以便`appsink`能够正常发出`new-sample`信号。



 启动pipeline，等待消息和最后的清理资源都和以前的没什么区别。下面主要讲解注册的回调函数：

### need-data

```c
/* This signal callback triggers when appsrc needs data. Here, we add an idle handler
 * to the mainloop to start pushing data into the appsrc */
static void start_feed (GstElement *source, guint size, CustomData *data) {
  if (data->sourceid == 0) {
    g_print ("Start feeding\n");
    data->sourceid = g_idle_add ((GSourceFunc) push_data, data);
  }
}
```

当`appsrc`的内部队列缺乏数据的时候就会触发上述回调，在这个回调函数中唯一做的事就是使用`g_idle_add()`注册了一个GLib空闲函数，在空闲函数中将不断向`appsrc`的传递数据只知道它的内部队列队满。GLib空闲函数是当它的主循环处于“空闲”状态时将被调用的方法，也就是说当前没有更高优先级的任务需要执行。调用GLib空闲函数需要用户线初始化并启动一个`GMainLoop`(推荐阅读[GMainLoop]()以获得更多关于`GMainLoop`的信息)。

这时`appsrc`允许的多种方法中的一个。事实上buffer并不需要使用GLib从主线程传递给`appsrc`，也不一定需要使用`need-data`和`enough-data`信号来与`appsrc`同步(据说是最方便的)。

**注：**如前文所说，流由GStreamer的单独线程处理，在实际的应用程序开发中appsrc的数据来源总是其他线程，数据的消耗有应用程序自行管理，通常消耗数据的速度足够快因此并不特别处理appsrc的enough-data信号。

我们维护`g_idle_add()`返回的`sourceid`，稍后需要禁用它。

### enough-data

```c
/* This callback triggers when appsrc has enough data and we can stop sending.
 * We remove the idle handler from the mainloop */
static void stop_feed (GstElement *source, CustomData *data) {
  if (data->sourceid != 0) {
    g_print ("Stop feeding\n");
    g_source_remove (data->sourceid);
    data->sourceid = 0;
  }
}
```

这个函数当appsrc内部的队列满的时候调用，所以我们需要停止发送数据。这里我们简单地用g_source_remove()来把idle函数注销。

### push-buffer

```c
/* This method is called by the idle GSource in the mainloop, to feed CHUNK_SIZE bytes into appsrc.
 * The ide handler is added to the mainloop when appsrc requests us to start sending data (need-data signal)
 * and is removed when appsrc has enough data (enough-data signal).
 */
static gboolean push_data (CustomData *data) {
  GstBuffer *buffer;
  GstFlowReturn ret;
  int i;
  gint16 *raw;
  gint num_samples = CHUNK_SIZE / 2; /* Because each sample is 16 bits */
  gfloat freq;

  /* Create a new empty buffer */
  buffer = gst_buffer_new_and_alloc (CHUNK_SIZE);

  /* Set its timestamp and duration */
  GST_BUFFER_TIMESTAMP (buffer) = gst_util_uint64_scale (data->num_samples, GST_SECOND, SAMPLE_RATE);
  GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale (num_samples, GST_SECOND, SAMPLE_RATE);

  /* Generate some psychodelic waveforms */
  raw = (gint16 *)GST_BUFFER_DATA (buffer);
```

我们使用上述函数向`appsrc`传递数据，GLib将以自己的频率和速度调用它(调用不受用户控制)，但是我们连接了`enough-data`信号以确保`appsrc`队满的时候能够停掉它。

它的第一个任务是使用`gst_buffer_new_and_allocate()`申请了一个给定大小的GstBuffer(在这个例子中是1024字节)。

我们计算我们生成的采样数据的数据量，把数据存在`CustomData.num_samples`里面，这样我们可以用GstBuffer提供的`GST_BUFFER_TIMESTAMP`宏来生成buffer的时间戳。

 `gst_util_uint64_scale()`是一个实用函数，用于缩放数据，确保不会溢出。

申请的buffer内存可以使用GstBuffer提供的`GST_BUFFER_DATA`宏来访问，在使用过程中要注意申请内存的大小以免操作越界。

**注：**`GST_BUFFER_DATA`等价于`gst_buffer_map (buffer, &map, GST_MAP_WRITE);`。

这里跳过波形的生成部分，因为这不是本教程要讲述的内容。

```c
/* Push the buffer into the appsrc */
g_signal_emit_by_name (data->app_source, "push-buffer", buffer, &ret);

/* Free the buffer now that we are done with it */
gst_buffer_unref (buffer);
```

一旦我们的buffer已经准备好，我们把带着使用`push-buffer`动作信号将这个buffer传给`appsrc`，然后就调用`gst_buffer_unref()`方法，因为我们不会再用到它了。

### new-sample

```c
/* The appsink has received a buffer */
static GstFlowReturn new_sample (GstElement *sink, CustomData *data) {
  GstSample *sample;
  /* Retrieve the buffer */
  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (sample) {
    /* The only thing we do in this example is print a * to indicate a received buffer */
    g_print ("*");
    gst_sample_unref (sample);
    return GST_FLOW_OK;
  }
  return GST_FLOW_ERROR;
}
```

最后，这个函数将在appsin接收到buffer数据的时候调用，我们使用`pull-sample`动作信号来获取buffer，然后向屏幕输出一个`*`以说明appsink成功接收到数据。我们可以使用GstBuffer提供的`GST_BUFFER_DATA`宏获取buffer的数据指针，使用`GST_BUFFER_SIZE`宏来获取buffer的数据大小。注意`appsink`接收到的buffer不一定和`push_data`函数中生成的buffer一致，因为在这个pipeline分支路径上的任何elements都能够修改经过它的buffer(但不是这个例子，本例中buffer仅经过一个`tee`，而`tee`并未改动buffer的内容)。

随着我们使用`gst_buffer_unref()`释放buffer，本教程也到此为止。

## 总结

这篇教程展示了应用程序如何：

- 如何使用`appsrc `元素向pipeline插入数据。
- 如何使用`appsink`元素从pipeline中检索数据。
- 如何通过GstBuffer操作从pipeline中取出的数据。

[Playback tutorial 3: Short-cutting the pipeline]()中使用基于playbin的pipeline以另外一种方式实现了相同的目标。

**注：**关于应用程序与GStreamer pipeline的数据交互，可以阅读[GStreamer App](https://ricardolu.gitbook.io/gstreamer/application-development/app)以获得更多实用信息。

原文地址：https://gstreamer.freedesktop.org/documentation/tutorials/basic/short-cutting-the-pipeline.html#


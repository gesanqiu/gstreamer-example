# Playback tutorial 4: Progressive streaming

## Goal

Basic tutorial 12: Streaming展示了如何在糟糕的网络情况下提高用户体验，通过使用缓冲机制。这篇教程是Basic tutorial 12: Streaming的进一步拓展——启用流媒体的本地存储，并描述了这种技术的优点。其中，主要展示了：

- 如何启用渐进式下载。
- 如何知道已下载的内容。
- 如何知道已下载内容的位置。
- 如何限制保存的下载数据的数量。

## Introduction

当流启动，将从互联网获取数据，为了保证流畅的播放，保留了一小块未来数据缓冲区。然而，数据将在它被播放或渲染后立即丢弃（程序中不会存在过去的数据缓冲）。这意味着，假如用户想要从过去的某个时刻开始回放，数据需要重新下载。

为流媒体量身定制的媒体播放器，例如Youtube，通常将所有下载的数据存储在本地，以防意外情况。通常会使用一个图形窗口来展示当前文件的下载进度。`playbin`通过`DOWNLOAD`标记提供了类似的功能，为了更快的播放已下载的数据，`playbin`能够将媒体保存到一个本地临时文件中。

本教程同时展示了如何使用Buffer Query，它允许知道文件的哪些部分可用。

## A network-resilient example with local storage

```c++
#include <gst/gst.h>
#include <string.h>

#define GRAPH_LENGTH 78

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_DOWNLOAD      = (1 << 7) /* Enable progressive download (on selected formats) */
} GstPlayFlags;

typedef struct _CustomData {
  gboolean is_live;
  GstElement *pipeline;
  GMainLoop *loop;
  gint buffering_level;
} CustomData;

static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  g_free (location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}

static void cb_message (GstBus *bus, GstMessage *msg, CustomData *data) {

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR: {
      GError *err;
      gchar *debug;

      gst_message_parse_error (msg, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      gst_element_set_state (data->pipeline, GST_STATE_READY);
      g_main_loop_quit (data->loop);
      break;
    case GST_MESSAGE_BUFFERING:
      /* If the stream is live, we do not care about buffering. */
      if (data->is_live) break;

      gst_message_parse_buffering (msg, &data->buffering_level);

      /* Wait until buffering is complete before start/resume playing */
      if (data->buffering_level < 100)
        gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      else
        gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    case GST_MESSAGE_CLOCK_LOST:
      /* Get a new clock */
      gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
      gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
      break;
    default:
      /* Unhandled message */
      break;
    }
}

static gboolean refresh_ui (CustomData *data) {
  GstQuery *query;
  gboolean result;

  query = gst_query_new_buffering (GST_FORMAT_PERCENT);
  result = gst_element_query (data->pipeline, query);
  if (result) {
    gint n_ranges, range, i;
    gchar graph[GRAPH_LENGTH + 1];
    gint64 position = 0, duration = 0;

    memset (graph, ' ', GRAPH_LENGTH);
    graph[GRAPH_LENGTH] = '\0';

    n_ranges = gst_query_get_n_buffering_ranges (query);
    for (range = 0; range < n_ranges; range++) {
      gint64 start, stop;
      gst_query_parse_nth_buffering_range (query, range, &start, &stop);
      start = start * GRAPH_LENGTH / (stop - start);
      stop = stop * GRAPH_LENGTH / (stop - start);
      for (i = (gint)start; i < stop; i++)
        graph [i] = '-';
    }
    if (gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position) &&
        GST_CLOCK_TIME_IS_VALID (position) &&
        gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &duration) &&
        GST_CLOCK_TIME_IS_VALID (duration)) {
      i = (gint)(GRAPH_LENGTH * (double)position / (double)(duration + 1));
      graph [i] = data->buffering_level < 100 ? 'X' : '>';
    }
    g_print ("[%s]", graph);
    if (data->buffering_level < 100) {
      g_print (" Buffering: %3d%%", data->buffering_level);
    } else {
      g_print ("                ");
    }
    g_print ("\r");
  }

  return TRUE;

}

int main(int argc, char *argv[]) {
  GstElement *pipeline;
  GstBus *bus;
  GstStateChangeReturn ret;
  GMainLoop *main_loop;
  CustomData data;
  guint flags;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Initialize our data structure */
  memset (&data, 0, sizeof (data));
  data.buffering_level = 100;

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);
  bus = gst_element_get_bus (pipeline);

  /* Set the download flag */
  g_object_get (pipeline, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_DOWNLOAD;
  g_object_set (pipeline, "flags", flags, NULL);

  /* Uncomment this line to limit the amount of downloaded data */
  /* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  } else if (ret == GST_STATE_CHANGE_NO_PREROLL) {
    data.is_live = TRUE;
  }

  main_loop = g_main_loop_new (NULL, FALSE);
  data.loop = main_loop;
  data.pipeline = pipeline;

  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", G_CALLBACK (cb_message), &data);
  g_signal_connect (pipeline, "deep-notify::temp-location", G_CALLBACK (got_location), NULL);

  /* Register a function that GLib will call every second */
  g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);

  g_main_loop_run (main_loop);

  /* Free resources */
  g_main_loop_unref (main_loop);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_print ("\n");
  return 0;
}
```

这个例程将打开一个窗口并播放一个含有音频的电影。这段媒体是从互联网获取的，所以窗口可能需要一定的时间才会出现，这取决于你的网络连接速度。在控制台窗口你将看到一条表明媒体存储位置的信息，以及一个文本格式的图形代表了下载进度和当前播放进度。一条缓冲消息将在请求缓冲时打印，但当你的网速足够快时这条消息不会出现。

## Walkthrough

### Setup

```c++
/* Set the download flag */
g_object_get (pipeline, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_DOWNLOAD;
g_object_set (pipeline, "flags", flags, NULL);
```

通过设置这个flag，`playbin`告诉它内部的queue（实际上是一个`queue2`元素）来存储所有下载的数据。

```c++
g_signal_connect (pipeline, "deep-notify::temp-location", G_CALLBACK (got_location), NULL);
```

当`GstObject`元素（例如`playbin`）任何子元素的属性改变时都将发出`deep-notify`信号。在这个例子中，我们关注`deep-notify::temp-location`的变化，它将指明`queue2`决定存储下载的数据的位置。

```c++
static void got_location (GstObject *gstobject, GstObject *prop_object, GParamSpec *prop, gpointer data) {
  gchar *location;
  g_object_get (G_OBJECT (prop_object), "temp-location", &location, NULL);
  g_print ("Temporary file: %s\n", location);
  g_free (location);
  /* Uncomment this line to keep the temporary file after the program exits */
  /* g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL); */
}
```

`got_location`将读取`queue2`中的`temp-location`属性并将其打印到屏幕上。

当pipeline的状态从`PAUSE`切换到`READY`时，这个临时文件将被删除。正如注释语句所言，你可以将`queue2`的`temp-remove`属性设为`FALSE`以禁用这一设置。

## User Interface

在main函数中，我们安装了一个定时器，用于每秒刷新一次UI。

```c++
/* Register a function that GLib will call every second */g_timeout_add_seconds (1, (GSourceFunc)refresh_ui, &data);
```

`refresh_ui`方法询问pipeline来获取当前已下载的文件部分以及当前正在播放的位置。它构建了一个文本图像来显示这个信息并将其打印到屏幕上，每次被调用都覆盖上一次的输出，使得其看起来像动画。

```c++
[---->-------                ]
```

破折号`-`标识已下载的部分，大于号`>`表示当前播放的位置（当暂停播放时将变为`X`）。但请记住假如你的网速足够快，你将看不到下载进度条（破折号）的加载；它将在刚开始就完成下载。

```c++
static gboolean refresh_ui (CustomData *data) {
  GstQuery *query;
  gboolean result;
  query = gst_query_new_buffering (GST_FORMAT_PERCENT);
  result = gst_element_query (data->pipeline, query);
```

我们在`refresh_ui`中做的第一件事就是使用`gst_query_new_buffering`构造了一个新的`GstQuery`缓冲查询对象并使用`gst_element_query`将其传递给`playbin`。在Basic tutorial 4: Time management中，我们一直知道如何使用特定的方法来完成像Position和Duration这样简单的查询。类似于缓冲区这种更加复杂的查询，需要使用更通常的`gst_element_query`接口。

关于缓冲区的查询可以是各种`GstFormat`格式的，包含TIME，BYTES，PERCENTAGE等等。但不是所有的elements都能响应所有格式的查询，因此你需要检车你的pipeline支持哪些格式。假如`gst_element_query`返回`TRUE`，这代表查询成功。查询的结果别保存在传入的`GstQuery`结构体中，然后我们可以使用多种解析方法提取出想要的信息：

```c++
n_ranges = gst_query_get_n_buffering_ranges (query);
for (range = 0; range < n_ranges; range++) {
  gint64 start, stop;
  gst_query_parse_nth_buffering_range (query, range, &start, &stop);
  start = start * GRAPH_LENGTH / (stop - start);
  stop = stop * GRAPH_LENGTH / (stop - start);
  for (i = (gint)start; i < stop; i++)
    graph [i] = '-';
}
```

数据不需要从文件的开头连续下载：例如，搜索（跳播）可能会迫使用户从一个新的位置开始下载，并留下已下载的数据块。因此`gst_query_get_n_buffering_ranges`将返回文件块的数目或者是已下载的数据的范围，然后我们可以使用`gst_query_parse_nth_buffering_range`来获取每个范围的位置和大小。

查询的返回值的类型将取决于`gst_query_new_buffering`查询什么格式的信息，在这个例子中是缓冲进度的百分比。这些只将用于生成表示下载进度的文本图像。

```c++
if (gst_element_query_position (data->pipeline, GST_FORMAT_TIME, &position) &&
    GST_CLOCK_TIME_IS_VALID (position) &&
    gst_element_query_duration (data->pipeline, GST_FORMAT_TIME, &duration) &&
    GST_CLOCK_TIME_IS_VALID (duration)) {
  i = (gint)(GRAPH_LENGTH * (double)position / (double)(duration + 1));
  graph [i] = data->buffering_level < 100 ? 'X' : '>';
}
```

接下来将查询当前播放的位置。它可以以PERCENT的格式查询，因此代码将和查询范围差不多，但目前对于当前播放位置的PERCENT格式的查询支持还不完善，因此我们使用TIME格式代替，并且查询持续时间来计算百分比。

当前播放的位置使用一个`>`或者`X`来表示，这取决于缓冲级别。当缓冲级别低于100%时，`cb_message`将把pipeline的状态设置为`PAUSE`，于是打印的是`X`。当缓冲级别为100%时，`cb_message`将把pipeline的状态设置为`PLAYING`，打印`>`。

注：由于开发板的网络原因，我并没能将例程运行起来，因此文档中关于进度的解释我其实并没有弄明白，尤其是上面绘制进度的start和stop的计算。根据我的理解：pipeline会等待整个媒体文件缓冲完成才会开始播放，在缓冲完成之前其实打印的都是`X`，例程也并没有支持动态的切换pipeline的状态，因此这里的播放和暂停与实际播放器能够完成的动态交互不太一样。

```c++
if (data->buffering_level < 100) {
  g_print (" Buffering: %3d%%", data->buffering_level);
} else {
  g_print ("                ");
}
```

最后，假如缓冲级别低于100%，我们将打印这个信息。

### Limiting the size of the downloaded file

```c++
/* Uncomment this line to limit the amount of downloaded data */
/* g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL); */
```

这减少了临时文件的大小，通过覆盖已经播放的区域。观察下载栏，可以看出文件中保持哪些区域可用。

## Conclusion

这篇教程展示了：

- `playbin`如何通过`GST_PLAY_FLAG_DOWNLOAD`标识实现平滑的下载。
- 如何通过缓冲区查询`GstQuery`已下载的内容。
- 如何通过`deep-notify::temp-location`获取下载文件的存储位置。
- 如何通过`ring-buffer-max-size`限制`playbin`下载的临时文件的大小。

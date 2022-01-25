# Playback tutorial 1: Playbin usage

## Goal

使用`playbin`，我们可以很方便的构建一个完整的播放pipeline而不需要做太多工作。这篇教程将展示如何进一步定制`playbin`以防它的默认值不符合我们特定的需求。

在这篇教程中我们将学习：

- 如何找出一个文件中包含多少个流，以及如何在这些流之间切换。
- 如何收集关于每个流的信息。

## Introduction

往往，多音频、视频和字母流能够被嵌入在一个单独的文件中。最常见的情况是电影，它含有一个视频和音频流（立体声或5.1声道被视作单个流）。为了适应不同的语言，使用一个视频流和多个音频流的电影也越来越常见。这种情况下，用户选择一个音频流，应用程序将播放它而忽略其他的音频流。

为了能够选择适当的流，用户需要知道这些流的确切信息，例如它们的语言。这些信息以一种“metadata”的格式被内嵌在流中，这篇教程将展示如何检索它。

字幕也可以与音频和视频一起嵌入到文件中，关于字幕的处理细节将在[Playback tutorial 2: Subtitle management]()中讨论。最后，在单个文件中也可以找到多个视频流，例如，在同一场景的多个角度的DVD中，但它们有点罕见。

注：将多个流嵌入到一个单独的文件中被称为“multiplexing”或“muxing”（通常翻译为“复用”），这类文件被称为“容器”。常见的容器格式包括.mkv，.qt，.mov，.mp4，.ogg和.webm。检索容器文件中的每个流的行为被称为“demultiplexing”或“demuxing”（通常被翻译为“解复用”）。

## The multilingual player

```c++
// playback-tutorial-1.c
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */

  gint n_video;          /* Number of embedded video streams */
  gint n_audio;          /* Number of embedded audio streams */
  gint n_text;           /* Number of embedded subtitle streams */

  gint current_video;    /* Currently playing video stream */
  gint current_audio;    /* Currently playing audio stream */
  gint current_text;     /* Currently playing subtitle stream */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;

/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT          = (1 << 2)  /* We want subtitle output */
} GstPlayFlags;

/* Forward definition for the message and keyboard processing functions */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstStateChangeReturn ret;
  gint flags;
  GIOChannel *io_stdin;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.playbin = gst_element_factory_make ("playbin", "playbin");

  if (!data.playbin) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.playbin, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_cropped_multilingual.webm", NULL);

  /* Set flags to show Audio and Video but ignore Subtitles */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
  flags &= ~GST_PLAY_FLAG_TEXT;
  g_object_set (data.playbin, "flags", flags, NULL);

  /* Set connection speed. This will affect some internal decisions of playbin */
  g_object_set (data.playbin, "connection-speed", 56, NULL);

  /* Add a bus watch, so we get notified when a message arrives */
  bus = gst_element_get_bus (data.playbin);
  gst_bus_add_watch (bus, (GstBusFunc)handle_message, &data);

  /* Add a keyboard watch so we get notified of keystrokes */
#ifdef G_OS_WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);

  /* Start playing */
  ret = gst_element_set_state (data.playbin, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.playbin);
    return -1;
  }

  /* Create a GLib Main Loop and set it to run */
  data.main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (data.main_loop);

  /* Free resources */
  g_main_loop_unref (data.main_loop);
  g_io_channel_unref (io_stdin);
  gst_object_unref (bus);
  gst_element_set_state (data.playbin, GST_STATE_NULL);
  gst_object_unref (data.playbin);
  return 0;
}

/* Extract some metadata from the streams and print it on the screen */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str;
  guint rate;

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &data->n_video, NULL);
  g_object_get (data->playbin, "n-audio", &data->n_audio, NULL);
  g_object_get (data->playbin, "n-text", &data->n_text, NULL);

  g_print ("%d video stream(s), %d audio stream(s), %d text stream(s)\n",
    data->n_video, data->n_audio, data->n_text);

  g_print ("\n");
  for (i = 0; i < data->n_video; i++) {
    tags = NULL;
    /* Retrieve the stream's video tags */
    g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
    if (tags) {
      g_print ("video stream %d:\n", i);
      gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
      g_print ("  codec: %s\n", str ? str : "unknown");
      g_free (str);
      gst_tag_list_free (tags);
    }
  }

  g_print ("\n");
  for (i = 0; i < data->n_audio; i++) {
    tags = NULL;
    /* Retrieve the stream's audio tags */
    g_signal_emit_by_name (data->playbin, "get-audio-tags", i, &tags);
    if (tags) {
      g_print ("audio stream %d:\n", i);
      if (gst_tag_list_get_string (tags, GST_TAG_AUDIO_CODEC, &str)) {
        g_print ("  codec: %s\n", str);
        g_free (str);
      }
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      if (gst_tag_list_get_uint (tags, GST_TAG_BITRATE, &rate)) {
        g_print ("  bitrate: %d\n", rate);
      }
      gst_tag_list_free (tags);
    }
  }

  g_print ("\n");
  for (i = 0; i < data->n_text; i++) {
    tags = NULL;
    /* Retrieve the stream's subtitle tags */
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      g_print ("subtitle stream %d:\n", i);
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    }
  }

  g_object_get (data->playbin, "current-video", &data->current_video, NULL);
  g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
  g_object_get (data->playbin, "current-text", &data->current_text, NULL);

  g_print ("\n");
  g_print ("Currently playing video stream %d, audio stream %d and text stream %d\n",
    data->current_video, data->current_audio, data->current_text);
  g_print ("Type any number and hit ENTER to select a different audio stream\n");
}

/* Process messages from GStreamer */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data) {
  GError *err;
  gchar *debug_info;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
      gst_message_parse_error (msg, &err, &debug_info);
      g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
      g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
      g_clear_error (&err);
      g_free (debug_info);
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_EOS:
      g_print ("End-Of-Stream reached.\n");
      g_main_loop_quit (data->main_loop);
      break;
    case GST_MESSAGE_STATE_CHANGED: {
      GstState old_state, new_state, pending_state;
      gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->playbin)) {
        if (new_state == GST_STATE_PLAYING) {
          /* Once we are in the playing state, analyze the streams */
          analyze_streams (data);
        }
      }
    } break;
  }

  /* We want to keep receiving messages */
  return TRUE;
}

/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    int index = g_ascii_strtoull (str, NULL, 0);
    if (index < 0 || index >= data->n_audio) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid audio stream index, set the current audio stream */
      g_print ("Setting current audio stream to %d\n", index);
      g_object_set (data->playbin, "current-audio", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}
```

```shell
gcc playback-tutorial-1.c -o playback-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0`
```

这个例程将打开一个窗口并播放一个含有音频的电影。这段媒体是从互联网获取的，所以窗口可能需要一定的时间才会出现，这取决于你的网络连接速度。这段媒体含有的音频流数量将在终端上打印，用户能够通过输入一个数字并按下enter按键，从一个音频流切换到另一个音频流。当然，切换会有一定的延迟。

请牢记这里没有延迟管理（缓冲），因此如果连接速度较慢，电影可能会在几秒钟后停止。可以阅读[Basic Tutorial 12: Streaming]()来解决这个问题。

## Walkthrough

```c++
/* Structure to contain all our information, so we can pass it around */
typedef struct _CustomData {
  GstElement *playbin;  /* Our one and only element */

  gint n_video;          /* Number of embedded video streams */
  gint n_audio;          /* Number of embedded audio streams */
  gint n_text;           /* Number of embedded subtitle streams */

  gint current_video;    /* Currently playing video stream */
  gint current_audio;    /* Currently playing audio stream */
  gint current_text;     /* Currently playing subtitle stream */

  GMainLoop *main_loop;  /* GLib's Main Loop */
} CustomData;
```

如往常一样，我们将所有我们需要的变量放入一个结构体中，以便我们能够在函数之间传递它们。在这篇教程中我们需要知道每种流的数量和当前在播放的流。并且我们将使用一种不同的机制来等待允许交互的消息，所以我们需要使用GLib的`main loop`对象。

```c++
/* playbin flags */
typedef enum {
  GST_PLAY_FLAG_VIDEO         = (1 << 0), /* We want video output */
  GST_PLAY_FLAG_AUDIO         = (1 << 1), /* We want audio output */
  GST_PLAY_FLAG_TEXT          = (1 << 2)  /* We want subtitle output */
} GstPlayFlags;
```

之后我们将设置一些`playbin`的运行标记。我们希望有一个方便的枚举，允许轻松操纵这些标志，但由于playbin是一个插件，而不是GStreamer核心的一部分，因此我们无法使用此枚举。技巧就是在我们的代码中简单的声明一个这样的枚举，像`playbin`的参考文档中的`GstPlayFlags`一样。GObject允许内省，因此这些flags能够在运行时被提取出来而不需要通过这类技巧，而是以一种更加笨重的方式。

```c++
/* Forward definition for the message and keyboard processing functions */
static gboolean handle_message (GstBus *bus, GstMessage *msg, CustomData *data);
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data);
```

预声明两个我们将用到的回调函数。`handle_message`用于处理GStreamer message，之前的教程中已经交接过了。`handle_keyboard`用于处理键盘敲击事件，因为这篇教程提供了有限的交互方式。

我们跳过创建pipeline的部分，它仅仅创建了`playbin`插件并将它的`uri`属性设置为我们的测试媒体。`playbin`这个插件自身就是一个pipeline，并且在这篇教程中它就是pipeline中唯一的element，所以我们跳过pipeline的完整创建，直接使用`playbin`即可。

```c++
/* Set flags to show Audio and Video but ignore Subtitles */
g_object_get (data.playbin, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO;
flags &= ~GST_PLAY_FLAG_TEXT;
g_object_set (data.playbin, "flags", flags, NULL);
```

`playbin`的行为可以通过改变它的`flags`属性来改变。`flags`可以是`GstPlayFlags`的任意逻辑运算组合。最常用的值如下：

| Flag                      | Description                                                  |
| :------------------------ | :----------------------------------------------------------- |
| GST_PLAY_FLAG_VIDEO       | Enable video rendering. If this flag is not set, there will be no video output. |
| GST_PLAY_FLAG_AUDIO       | Enable audio rendering. If this flag is not set, there will be no audio output. |
| GST_PLAY_FLAG_TEXT        | Enable subtitle rendering. If this flag is not set, subtitles will not be shown in the video output. |
| GST_PLAY_FLAG_VIS         | Enable rendering of visualisations when there is no video stream. Playback tutorial 6: Audio visualization goes into more details. |
| GST_PLAY_FLAG_DOWNLOAD    | See Basic tutorial 12: Streaming and Playback tutorial 4: Progressive streaming. |
| GST_PLAY_FLAG_BUFFERING   | See Basic tutorial 12: Streaming and Playback tutorial 4: Progressive streaming. |
| GST_PLAY_FLAG_DEINTERLACE | If the video content was interlaced, this flag instructs playbin to deinterlace it before displaying it. |

在这篇教程中，出于演示目的，我们使能音频和视频，但是禁用字幕，其他值保留默认值（这也是为什么我们在用`g_object_set`覆写`falgs`值之前先使用`g_object_get`获取原有值）。

```c++
/* Set connection speed. This will affect some internal decisions of playbin */
g_object_set (data.playbin, "connection-speed", 56, NULL);
```

这个属性在这个例子中并没有太大的作用。`connection-speed`告知`playbin`当前网络连接的最大速度，因此，如果服务器中有多个版本的请求媒体可用，`playbin`会选择最合适的版本。这主要与`hls`或`rtsp`等流协议结合使用。

```c++
g_object_set (data.playbin, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_cropped_multilingual.webm", "flags", flags, "connection-speed", 56, NULL);
```

我们可以仅使用一次`g_object_set()`设置所有的属性，这也是为什么这个接口需要以`NULL`作为最后一个参数。

```c++
/* Add a keyboard watch so we get notified of keystrokes */
#ifdef _WIN32
  io_stdin = g_io_channel_win32_new_fd (fileno (stdin));
#else
  io_stdin = g_io_channel_unix_new (fileno (stdin));
#endif
  g_io_add_watch (io_stdin, G_IO_IN, (GIOFunc)handle_keyboard, &data);
```

这几行将一个回调函数与标准输入（键盘）连接起来。这里展示的这个机制由GLib实现，因此与GStreamer无关，所以这里不会深入讨论它。应用程序通常有自己的处理用户输入的手段，并且GStreamer不会做过多的干涉除了在[Tutorial 17: DVD playback]中简单讨论的导航接口。

```c++
/* Create a GLib Main Loop and set it to run */
data.main_loop = g_main_loop_new (NULL, FALSE);
g_main_loop_run (data.main_loop);
```

为了允许交互，我们不再手动论询GStreamer bus。取而代之的，我们创建一个`GMainLoop`并通过`g_main_loop_run()`将其设置为运行状态。这个函数将锁住线程直到`g_main_loop_quit()`被调用。同时它将在适当的时间调用我们之前注册的两个回调函数：当bus上出现消息的时候调用`handle_message`；当用户按下按键的时候调用`handle_keyboard`。

`handle_message`没有新内容，除了当pipeline切换到`PLAYING`状态时，它将调用`analyze_streams`函数：

```c++
/* Extract some metadata from the streams and print it on the screen */
static void analyze_streams (CustomData *data) {
  gint i;
  GstTagList *tags;
  gchar *str;
  guint rate;

  /* Read some properties */
  g_object_get (data->playbin, "n-video", &data->n_video, NULL);
  g_object_get (data->playbin, "n-audio", &data->n_audio, NULL);
  g_object_get (data->playbin, "n-text", &data->n_text, NULL);
```

如注释所言，这个函数仅收集媒体的信息并将它打印在屏幕上。视频流，音频流和字母流的数量可以直接通过`n-video`，`n-audio`和`n-text`属性获取到。

```c++
for (i = 0; i < data->n_video; i++) {
  tags = NULL;
  /* Retrieve the stream's video tags */
  g_signal_emit_by_name (data->playbin, "get-video-tags", i, &tags);
  if (tags) {
    g_print ("video stream %d:\n", i);
    gst_tag_list_get_string (tags, GST_TAG_VIDEO_CODEC, &str);
    g_print ("  codec: %s\n", str ? str : "unknown");
    g_free (str);
    gst_tag_list_free (tags);
  }
}
```

现在，对于每个流，我们想到提取出它的metadata。metadata作为标签存储在一个`GstTagList`中，这是一个以名字区分的数据片段列表。一个流的`GstTagList`可以通过`g_signal_emit_by_name()`还原，并且每个单独的标签可以使用`gst_tag_list_get_*`提取出来，例如`gst_tag_list_get_string()`。

注：这种相当不直观的检索标记列表的方法被称作“Action Signal“。Action signals由应用程序向特定的element发送，element将执行一个动作并返回结果。它们的行为类似于动态函数调用，即方法以信号名称而不是内存地址来标识。Action signals列表可以在插件的文档中找到。

`playbin`定义了三个action signals用于检索metadata：`get-video-tags`， `get-audio-tags` 和 `get-text-tags`。如果标签是标准化的，那么名称和列表可以在`GstTagList`文档中找到。在这个例子中，我们感兴趣的是流的语言和各个流的编解码信息。

```c++
g_object_get (data->playbin, "current-video", &data->current_video, NULL);
g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
g_object_get (data->playbin, "current-text", &data->current_text, NULL);
```

一旦我们提取到了所有我们需要的metadata，我们使用另外3个属性获取`playbin`当前选中的流：`current-video`，`current-audio` 和 `current-text`。

值得注意的是我们应该总是使用接口检查当前选中的流而不是依赖于假设。因为多个内部条件可以使playbin在不同的执行中表现不同。此外，列出的流的顺序可能每次运行都不一样，因此检查元数据以识别一个特定流变得至关重要。

```c++
/* Process keyboard input */
static gboolean handle_keyboard (GIOChannel *source, GIOCondition cond, CustomData *data) {
  gchar *str = NULL;

  if (g_io_channel_read_line (source, &str, NULL, NULL, NULL) == G_IO_STATUS_NORMAL) {
    int index = g_ascii_strtoull (str, NULL, 0);
    if (index < 0 || index >= data->n_audio) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid audio stream index, set the current audio stream */
      g_print ("Setting current audio stream to %d\n", index);
      g_object_set (data->playbin, "current-audio", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}
```

最后，我们我们允许用户切换正在播放的音频流。这个非常基础的函数从标准输入（键盘）读取一个字符串，被转义为一个数字，并尝试设置`playbin`的`current-audio`属性。

切记这种切换不是立即生效的。一些之前解码好的音频数据将仍然在pipeline中流动，虽然新的流已经开始解码。延迟取决于容器中流的特定多路复用和`playbin`的内部queue的长度（这取决于网络状况）。

如果你运行这个例程，你将能够在播放电影的同时从一种语言切换到另外一个语言，通过按下0，1或2（再按Enter）。

## Conclusion

这篇教程展示了：

- `playbin`的更多属性：`flags`， `connection-speed`， `n-video`， `n-audio`， `n-text`， `current-video`，`current-audio` 和 `current-text`。
- 如何通过`g_signal_emit_by_name()`检索一个流的标签列表。
- 如何通过`gst_tag_list_get_string()`或 `gst_tag_list_get_uint()`检索特定的tag。
- 如何通过简单的修改`current-audio`属性来切换当前播放的音频流。

下一篇播放教程将展示如何处理字幕，包括内嵌字幕和外挂字幕。
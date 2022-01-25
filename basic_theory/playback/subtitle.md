# Playback tutorial 2: Subtitle management

## Goal

这篇教程与上一篇非常相似，但是我们将切换字幕流而不是音频流。我们将学到：

- 如何选择字幕流。
- 如何添加外挂字幕。
- 如何自定义字幕的字体。

## Introduction

我们已经知道（通过之前的教程）容器文件可以拥有多个音视频流，并且我们可以通过修改`current-audio`和`current-video`属性从中选择要播放的流。切换字幕流也同样简单。

值得注意的是，就像音频和视频一样，`playbin`负责为字幕选择正确的解码器，并且GStreamer的插件结构允许添加对新格式的支持就像复制文件一样简单。这些细节都对应用程序开发者不可见。

除了内嵌在容器中的字幕，`playbin`还提供了从外部URI添加额外字幕流的可能性。

这篇教程打开了一个包含了5个字幕流的文件，并且通过其他文件添加了一个字幕流（瑞士语）。

## The multilingual player with subtitles

```c++
#include <stdio.h>
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
  g_object_set (data.playbin, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.ogv", NULL);

  /* Set the subtitle URI to play and some font description */
  g_object_set (data.playbin, "suburi", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer_gr.srt", NULL);
  g_object_set (data.playbin, "subtitle-font-desc", "Sans, 18", NULL);

  /* Set flags to show Audio, Video and Subtitles */
  g_object_get (data.playbin, "flags", &flags, NULL);
  flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
  g_object_set (data.playbin, "flags", flags, NULL);

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
    g_print ("subtitle stream %d:\n", i);
    g_signal_emit_by_name (data->playbin, "get-text-tags", i, &tags);
    if (tags) {
      if (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE, &str)) {
        g_print ("  language: %s\n", str);
        g_free (str);
      }
      gst_tag_list_free (tags);
    } else {
      g_print ("  no tags found\n");
    }
  }

  g_object_get (data->playbin, "current-video", &data->current_video, NULL);
  g_object_get (data->playbin, "current-audio", &data->current_audio, NULL);
  g_object_get (data->playbin, "current-text", &data->current_text, NULL);

  g_print ("\n");
  g_print ("Currently playing video stream %d, audio stream %d and subtitle stream %d\n",
      data->current_video, data->current_audio, data->current_text);
  g_print ("Type any number and hit ENTER to select a different subtitle stream\n");
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
    int index = atoi (str);
    if (index < 0 || index >= data->n_text) {
      g_printerr ("Index out of bounds\n");
    } else {
      /* If the input was a valid subtitle stream index, set the current subtitle stream */
      g_print ("Setting current subtitle stream to %d\n", index);
      g_object_set (data->playbin, "current-text", index, NULL);
    }
  }
  g_free (str);
  return TRUE;
}
```

这个例程将打开一个窗口并播放一个含有音频的电影。这段媒体是从互联网获取的，所以窗口可能需要一定的时间才会出现，这取决于你的网络连接速度。这段媒体含有的字幕流数量将在终端上打印，用户能够通过输入一个数字并按下enter按键，从一个字幕流切换到另一个字幕流。当然，切换会有一定的延迟。

请牢记这里没有延迟管理（缓冲），因此如果连接速度较慢，电影可能会在几秒钟后停止。可以阅读[Basic Tutorial 12: Streaming]()来解决这个问题。

## Walkthrough

这个例程仅在[Playback tutorial 1: Playbin usage]()基础上做了部分修改，所以让我们关注这些修改。

```c++
/* Set the subtitle URI to play and some font description */
g_object_set (data.playbin, "suburi", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer_gr.srt", NULL);
g_object_set (data.playbin, "subtitle-font-desc", "Sans, 18", NULL);
```

在设置好媒体文件URI之后，我们设置了`suburi`属性，它告诉`playbin`包含字幕流的文件位置。在这个例程中，媒体文件本身就包含了多个字幕流，因此通过`suburi`属性设置的字幕流将会被加入字幕列表中并成为当前选中的字幕。

注意字幕流的metadata（例如字幕语言）在容器文件中，因此外挂字幕将不带有metadata。当运行例程时你会发现第一个字幕流没有语言标签。

`subtitle-font-desc`属性允许指定渲染字幕的字体。这里使用`Pango`库来渲染字体，你可以查阅它的文档以了解如何指定字体，尤其是[pango-font-description-from-string](http://developer.gnome.org/pango/stable/pango-Fonts.html#pango-font-description-from-string)函数。

简而言之，`subtitle-font-desc`属性值的格式是`[FAMILY-LIST] [STYLE-OPTIONS] [SIZE]`。`FAMILY-LIST`为字体，以`,`与后面的值隔开；`STYLE-OPTIONS`是字体属性列表，包含字体风格，变体，粗细和字间距，属性值以空格符隔开；`SIZE`是字号，是一个十进制数。

以下是几个可用的例子：

- sans bold 12
- serif, monospace bold italic condensed 16
- normal 10

常用的字体有：Normal，Sans，Monospace。

常用的格式有：Normal，Oblique（罗马斜体），Italic（意大利斜体）。

常用的粗细有：Ultra-Light，Light，Normal，Bold，Ultra-Bold，Heavy。

常用的变体有：Normal，Small_Caps （一种将小写字母以稍小的大写题目替换的格式）。

常用的字间距有：Ultra-Condensed，Extra-Condensed，Condensed，Semi-Condensed，Normal，Semi-Expanded，Expanded，Extra-Expanded，Ultra-Expanded。

```c++
/* Set flags to show Audio, Video and Subtitles */
g_object_get (data.playbin, "flags", &flags, NULL);
flags |= GST_PLAY_FLAG_VIDEO | GST_PLAY_FLAG_AUDIO | GST_PLAY_FLAG_TEXT;
g_object_set (data.playbin, "flags", flags, NULL);
```

我们设置`flags`以允许播放音频，视频和字幕。

例程剩下的内容和[Playback tutorial 1: Playbin usage]()一样，除了将键盘输入修改的属性从`current-audio`改为了`current-text`。和之前一样，切记流的改变不是立即生效的，因为在你切换的流显示之前仍然有大量的信息在pipeline中流动直到中止。

## Conclusion

这篇教程展示了`playbin`如何处理字幕，无论是内嵌字幕还是外挂字幕：

- `playbin`通过`n-text`和`current-text`属性来选择字幕。
- 外挂字幕可以通过`suburi`属性来加载指定。
- 字幕的外观可以通过`subtitle-font-desc`属性来自定义。

下一篇教程将展示如何改变播放的速度。
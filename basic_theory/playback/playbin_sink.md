# Playback tutorial 7: Custom playbin sinks

## Goal

`playbin`可以通过手动选择其音频和视频sink进行进一步定制。这允许应用程序仅依赖`playbin`提取和解码媒体数据然后自行管理数据的渲染/演示。这篇教程展示了：

- 如何替换`playbin`的sink。
- 如何使用一条复杂的pipeline作为sink。

## Introduction

`playbin`的两个属性允许用户选择自己想要的audio和video sinks：`audio-sink`和`video-sink`。应用程序仅需要初始化适当的`GstElement`并将其传递给`playbin`的这两个属性。

然而这个属性仅允许使用单个element作为sink。如果需要使用更加复杂的pipeline，例如一个均衡器加上一个audio sink，它们需要被包裹在一个bin中，这样对于`playbin`来说，这个bin看起来就像一个独立的element。

一个Bin（`GstBin`）是一个封装了部分pipeline的容器，通过bin这部分pipeline元素能够以一个独立的element的形式管理。例如，我们在所有教程中使用的`GstPipeline`实际是就是一个`GstBin`，只是它不再与其他外部element交互，即`GstPipeline`是最上（外）层的`GstBin`。Bin中的elements通过一个`GstGhostPad`与外部elements连接，`GstGhostPad`是一个仅将数据简单的从外部pad传递给指定的内部pad的接口。

![img](images/bin-element-ghost.png)

`GstBin`也是一个广义上的`GstElement`，因此请求element的地方也能够使用`GstBin`。

## An equalized player

```c++
#include <gst/gst.h>

int main(int argc, char *argv[]) {
  GstElement *pipeline, *bin, *equalizer, *convert, *sink;
  GstPad *pad, *ghost_pad;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Build the pipeline */
  pipeline = gst_parse_launch ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);

  /* Create the elements inside the sink bin */
  equalizer = gst_element_factory_make ("equalizer-3bands", "equalizer");
  convert = gst_element_factory_make ("audioconvert", "convert");
  sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
  if (!equalizer || !convert || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Create the sink bin, add the elements and link them */
  bin = gst_bin_new ("audio_sink_bin");
  gst_bin_add_many (GST_BIN (bin), equalizer, convert, sink, NULL);
  gst_element_link_many (equalizer, convert, sink, NULL);
  pad = gst_element_get_static_pad (equalizer, "sink");
  ghost_pad = gst_ghost_pad_new ("sink", pad);
  gst_pad_set_active (ghost_pad, TRUE);
  gst_element_add_pad (bin, ghost_pad);
  gst_object_unref (pad);

  /* Configure the equalizer */
  g_object_set (G_OBJECT (equalizer), "band1", (gdouble)-24.0, NULL);
  g_object_set (G_OBJECT (equalizer), "band2", (gdouble)-24.0, NULL);

  /* Set playbin's audio sink to be our sink bin */
  g_object_set (GST_OBJECT (pipeline), "audio-sink", bin, NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

这个例程将打开一个窗口并播放一个含有音频的电影。这段媒体是从互联网获取的，所以窗口可能需要一定的时间才会出现，这取决于你的网络连接速度。由于音频的高频部分被衰减，因此电影声音的低音部分将更清晰。

## Walkthrough

```c++
/* Create the elements inside the sink bin */
equalizer = gst_element_factory_make ("equalizer-3bands", "equalizer");
convert = gst_element_factory_make ("audioconvert", "convert");
sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
if (!equalizer || !convert || !sink) {
  g_printerr ("Not all elements could be created.\n");
  return -1;
}
```

上述代码实例化了所有组成sink-bin的elements。我们使用了一个`equalizer-3bands`和一个`autoaudiosink`插件，这两个插件中间插入了一个`audioconvert`，因为我们并不确定audio sink会需要什么样的capabilities（因为audio sink可能有硬件依赖）。

```c++
/* Create the sink bin, add the elements and link them */
bin = gst_bin_new ("audio_sink_bin");
gst_bin_add_many (GST_BIN (bin), equalizer, convert, sink, NULL);
gst_element_link_many (equalizer, convert, sink, NULL);
```

将所有的新建的elements加到一个bin中，并像链接pipeline elements一样连接他们。

```c++
pad = gst_element_get_static_pad (equalizer, "sink");
ghost_pad = gst_ghost_pad_new ("sink", pad);
gst_pad_set_active (ghost_pad, TRUE);
gst_element_add_pad (bin, ghost_pad);
gst_object_unref (pad);
```

现在我们需要为这个bin创建一个`GstGhostPad`，从而让这个bin能够与外部连接。这个Ghost Pad将与bin中的`equalizer-3bands`的sink-pad连接，这个sink-pad可以使用`gst_element_get_static_pad()`获取到。

Ghost Pad可以使用`gst_ghost_pad_new()`创建（它将指向我们指定的GstBin内部的pad），并使用`gst_pad_set_active()`使其生效。然后使用`gst_element_add_pad()`将其加入bin中，将Ghost Pad的控制权转移给bin，然后我们不需要关系它的释放。

到此为止，我们拥有了一个功能性的sink-bin，我们能像使用audio sink一样使用它。我们只需要将其与`playbin`连接：

```c++
/* Set playbin's audio sink to be our sink bin */
g_object_set (GST_OBJECT (pipeline), "audio-sink", bin, NULL);
```

只需将`playbin`上的`audio-sink`属性设置为新创建的sink bin即可。

```c++
/* Configure the equalizer */
g_object_set (G_OBJECT (equalizer), "band1", (gdouble)-24.0, NULL);
g_object_set (G_OBJECT (equalizer), "band2", (gdouble)-24.0, NULL);
```

最后修改了均衡器的设置，这部分是音频的处理，与教程无关，不做过多赘述。

## Conclusion

这篇教程展示了：

- 如何通过`audio-sink`和`video-sink`属性指定`playbin`的sink element。
- 如何将多个elements包裹为一个`GstBin`，使其能够像使用单个element一样被`playbin`使用。
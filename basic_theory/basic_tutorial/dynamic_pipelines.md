# Basic tutorial 3: Dynamic pipelines

## 目标

这篇教程展示了使用GStreamer需要的剩余基本概念，允许你随着数据流动来构建pipeline， 而不是在应用程序的一开始就定义一个完整的管道。

学习完这篇教程，你讲具备开始Playback tutorial的必要知识：

- 如何在连接elements的时候实现更好的控制。
- 如何获取感兴趣的事件通知以便及时做出处理。
- GStreamer States。

## 介绍

可以看到这篇教程的pipeline在设置为PLAYING状态之前都没有完成构建，这种行为是允许的。但是假如在播放之前没有完成，那么数据在到达pipeline的某个节点将上报一个错误信息并停止运行。

在这个例子中我们将打开一个多路复用的文件，音频和视频被存储在同一个容器文件中。负责响应打开多路复用文件的element被叫做解复用器，可以处理MKV、QT、MOV、Ogg、WMV等格式的容器文件。

GStreamer elements相互通信的端口称为pad，存在sink pad(数据通过它进入元素)和src pad(数据通过它退出元素)，根据定义很明显可以知道source element只有src pad，sink element只有sink pad，而filter element两者都有。

![img](images/dynamic_pipelines/src-element.png)

![img](images/dynamic_pipelines/filter-element.png)

![img](images/dynamic_pipelines/sink-element.png)



解复用器含有一个sink pad，复用数据经过它进入element；含有多个source pads，复用数据中的每路数据各一个。

![img](images/dynamic_pipelines/filter-element-multi.png)

下图是一个包含一个解复用器和两个分支的简单pipeline，一个分支处理音频数据，一个分支处理视频数据。

**注意此例图这不是本教程的pipeline。**

![img](images/dynamic_pipelines/simple-player.png)

处理解复用器的难点在于，直到收到一些数据以及有机会查看容器文件中的内容之前解复用器无法生成任何信息，即解复用器的source pad是动态生成的，在生成之前其他element无法与它连接，于是pipeline只能在这终止。

当解复用器收到足够多的信息，能够知道容器文件中媒体流的数量和类型时，它将开始创建source pad，这时我们就可以完成pipeline的构建。

**注：为了简单起见，本教程例子只连接音频pad而忽略视频pad。**

## Dynamic Hello World

### basic-tutorial-3.c

```c
#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *pad, CustomData *data);

int main(int argc, char *argv[]) {
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  data.source = gst_element_factory_make ("uridecodebin", "source");
  data.convert = gst_element_factory_make ("audioconvert", "convert");
  data.resample = gst_element_factory_make ("audioresample", "resample");
  data.sink = gst_element_factory_make ("autoaudiosink", "sink");

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.source || !data.convert || !data.resample || !data.sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline. Note that we are NOT linking the source at this
   * point. We will do it later. */
  gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.convert, data.resample, data.sink, NULL);
  if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.source, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);

  /* Connect to the pad-added signal */
  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n", GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n", debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
  GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad), GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  if (gst_pad_is_linked (sink_pad)) {
    g_print ("We are already linked. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
    g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  ret = gst_pad_link (new_pad, sink_pad);
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
}
```

## 工作流

### CustomData

```c
/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
  GstElement *pipeline;
  GstElement *source;
  GstElement *convert;
  GstElement *sink;
} CustomData;
```

在此前的教程中，我们以局部变量的形式维护所有我们需要的信息(GstElement指针)，但是由于本教程(和大部分应用程序)需要使用回调函数，为了便于处理我们将所有数据组织成一个结构体。

### Build Pipeline

```c
/* Create the elements */
data.source = gst_element_factory_make ("uridecodebin", "source");
data.convert = gst_element_factory_make ("audioconvert", "convert");
data.resample = gst_element_factory_make ("audioresample", "resample");
data.sink = gst_element_factory_make ("autoaudiosink", "sink");

```

GstElements的创建和连接如前文一样，在本教程中我们创建了一个`uridecodebin`，和[Basic tutorial 1: Hello world!]中的playbin一样，它也是Playback中的bin插件，它在内部实例化所有需要的elements(source, demuxers和decoders)以将URI解码成裸音频流和/或裸音频流。但和playbin不一样的是它并不包含解码之后的播放处理，因此和解复用器一样，它的source pad在初始化阶段是不可用的需要用户手动完成连接。

`audioconvert`是一个非常有用的插件，它能转换不同的音频格式，确保教程中的例子能够在任何平台上运行(各个平台上的音频解码器解码出的格式不一定符合audio sink的要求)。

`audioresample`是一个非常有用的插件，它能够转换不同的音频采样率，同样是为了确保教程中的例子能够在任何平台上运行(aduio sink不一定支持各个平台上的音频解码器解码出的音频采样率)。

`autoaudiosink`和前文中用到的`autovideosink`一样，它将把音频流渲染到声卡上。

```c
if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
  g_printerr ("Elements could not be linked.\n");
  gst_object_unref (data.pipeline);
  return -1;
}
```

这里我们将`audioconvert`，`audioresample`和`autoaudiosink`连接起来，但是并没有将它们和source连接，因为这这个阶段，`uridecodebin`还没有生成source pad，这部分工作将放在之后完成。

```c
/* Set the URI to play */
g_object_set (data.source, "uri", "https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm", NULL);
```

设置`uridecodebin`的`uri`属性。

### Signals

```c
/* Connect to the pad-added signal */
g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler), &data);
```

GSignals是GStreamer的一个重点，它们将在某些事件发生的时候以回调的方式通知你。这些信号以名字属性区分，每个GObject都有它自己的信号。

上述代码中我们使用`gst_signal_connect()`监听`uridecodebin`的`pad-added`信号，并为这个信号提供一个回调函数指针`pad_added_handler`并向回调传递一个用户数据指针`data`。GStreamer不会对用户数据指针进行处理，它直接将它传递给回调函数，因此回调函数可以使用主线程的数据。在本教程中，我们传递了一个`CustomData`结构体变量。

一个GstElement能够出发的signals可以在它的文档看到或者使用`gst-inspect-1.0`查看。

至此pipeline构建完成，我们可以设置PLAYING状态并开始播放以及监听bus中你感兴趣的messages。

### 回调函数

当source element最终获取到足够的信息从而开始生成数据的时候，它将创建source pads并且出发`pad-added`信号，这时将调用信号连接的回调函数 ：

```c
static void pad_added_handler (GstElement *src, GstPad *new_pad, CustomData *data) {
```

- `src`是出发信号的element，在本教程中是`uridecodebin`，信号处理程序的第一个参数总是触发它的对象。
- `new_pad`是`src` element刚刚添加的`GstPad`，这正是我们想要连接的pad。
- `data`是我们连接信号时传递的指针，在本教程中是一个`CustomData`指针。

```c
GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
```

我们从`CustomData`中获取`autoaudioconvert`element，并使用`gst_element_get_static_pad()`获取它的`sink pad`，要与`new_pad`连接的pad。在之前教程中我们连接elements，由GStreamer自行选择正确的pads，现在我们手动完成这部分连接。

```c
/* If our converter is already linked, we have nothing to do here */
if (gst_pad_is_linked (sink_pad)) {
  g_print ("We are already linked. Ignoring.\n");
  goto exit;
}
```

`uridecodebin`将生成尽可能多的pads，这取决于它能够出处理的数据类型，并且没生成一个pad，回调都会被调用一次，上述代码将避免我们尝试将`new_pad`与一个已经连接了的element连接。

```c
/* Check the new pad's type */
new_pad_caps = gst_pad_get_current_caps (new_pad, NULL);
new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
new_pad_type = gst_structure_get_name (new_pad_struct);
if (!g_str_has_prefix (new_pad_type, "audio/x-raw")) {
  g_print ("It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
  goto exit;
}
```

现在让我们来检查这个`new_pad`将输出的数据类型，因为本教程只关注音频数据。在这个pad生成之前我们已经创建并连接了一系列处理音频数据所需的elements，现在要做的就是把`new_pad`与这些elements连接起来。

`gst_pad_get_current_caps()`检查当前pad的`capabilities`(当前输出的数据类型)，它被包裹在一个GstCaps结构体中。用户可以使用`gst_pad_query_caps()`获取当前pad支持的所有caps。一个pad可以提供许多capabilities，因此GstCaps可以包含许多GstStructure，每一个GstStructure代表一个不同的`capability`。但一个pad当前的caps上只有一个GstStructure代表了单一的媒体格式，或者pad当前没有caps将返回一个`NULL`。

在教程中，我只关注音频数据，在这个pad中我们想要的只有音频`capability`，所以我们使用gst_caps_get_structure()检索pad的第一个GstStructure。

最后我们使用`gst_structure_get_name()`来恢复结构体的name成员，它包含媒体数据格式的主要描述。

假如GstStructure的name不是`audio/x-raw`那么意味着这个pad不是音频解码的数据的pad，目前我们不需要关注非音频数据，所以直接跳过。

```c
/* Attempt the link */
ret = gst_pad_link (new_pad, sink_pad);
if (GST_PAD_LINK_FAILED (ret)) {
  g_print ("Type is '%s' but link failed.\n", new_pad_type);
} else {
  g_print ("Link succeeded (type '%s').\n", new_pad_type);
}
```

`gst_pad_link()`尝试连接两个pads，连接顺序必须是source->sink，并且两个pads必须属于同一个bin/pipeline中的elements。

随着pad连接完成，uridecodebin将与其余elements连接起来，程序将得以正确运行直到遇到`ERROR`或是`EOS`。

## GStreamer States

在前两篇教程中我们都使用`gst_element_set_state()`来修改设置pipeline的状态为`PLAYING`以让pipeline运行，这里介绍其余的GStreamer States：

| State     | Description                                                  |
| :-------- | :----------------------------------------------------------- |
| `NULL`    | the NULL state or initial state of an element.               |
| `READY`   | the element is ready to go to PAUSED.                        |
| `PAUSED`  | the element is PAUSED, it is ready to accept and process data. Sink elements however only accept one buffer and then block(Preroll). |
| `PLAYING` | the element is PLAYING, the clock is running and the data is flowing. |

Pipeline只能在上表中相邻的两个状态之间变换，即无法从`NULL`直接改为`PLAYING`，必须经过`READY`和`PAUSED`两个中间态。但是假如你将pipeline设置为`PLAYING`, GStreamer将自动进行中间转换。

```c
case GST_MESSAGE_STATE_CHANGED:
  /* We are only interested in state-changed messages from the pipeline */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    g_print ("Pipeline state changed from %s to %s:\n",
        gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
  }
  break;
```

本教程添加了这段代码，用于监听有关状态更改的总线消息并将它们打印到屏幕上，以帮助理解状态转换。每个element都将关于其当前状态的消息放在总线上**(设置GST_DEBUG环境变量可以查看所有GStreamer elements的输出LOG)**，因此我们将它们过滤掉，只监听来自pipeline的消息。

大多数应用程序只需要关心`PLAYING`状态能否正常播放，`PAUSED`状态能否正常暂停以及`NULL`状态能否退出和回收资源

## 练习

对大多数程序员而言，动态连接GstPad一直是一个困难的话题，为了证明你掌握了它的连接，请在本教程的基础上添加一个`autovideosink`(可能在它之前还需要一个`videoconvert`)并将其与解复用器连接起来，以便你的pipeline能够在处理音频数据的同时处理视频数据。

假如你正确实现了，你的程序运行起来应该和[Basic tutorial 1: Hello world!](https://ricardolu.gitbook.io/gstreamer/basic-theory/basic-tutorial1-hello-world)一样能够看到和听到电影。

**注：**这个练习的实现代码可以参考[Build Pipeline](https://ricardolu.gitbook.io/gstreamer/application-development/build-pipeline)教程，在这个教程的`gst_element_factory_make()`章节我构建了一条符合这个联系要求的pipeline并成功运行起来。

## 总结

在这篇教程中，你学习了：

- 如何使用GSignals通知事件
- 如何直接连接GstPads而不是连接它们的父elements
- GStreamer element的状态

你结合以上内容构建了一条动态pipeline，这条pipeline不是在程序开始就被指定好的，而是在获取到所有媒体信息的情况下创建的。

这时你可以选择继续学习接下来的基础教程，或者是直接学习Playback教程以获得更多关于playbin element的信息。**个人的建议是跳过Basic tutorial 4/5(因为它们并不常用)，可以先完成Basic tutorial 67/8的学习，再开始Playback教程的学习。**


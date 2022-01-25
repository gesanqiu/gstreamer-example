# Basic tutorial 7: Multithreading and Pad Availability

## 目标

GStreamer自动处理多线程，但是在某些情况下，用户可能需要手动解耦线程。这篇教程将展示如何解耦线程以及完善关于Pad Availability的描述。更准确来说，这篇文档解释了：

- 如何为pipeline的某些部分创建新的线程。
- 什么是Pad Availability。
- 如何复制流。

## 介绍

### Multithreading

GStreamer是一个多线程的框架，这意味着在内部，它根据需要创建和销毁线程，例如，将流的处理从应用程序线程解耦。此外，插件也可以自由创建线程来处理它们的任务，例如视频解码器可以创建四个线程以充分利用CPU的四个核。

除此以外，应用程序在创建pipeline的时候可以明确的指定它的一个分支(pipeline的一部分)运行在不同的线程上(例如同时进行音频和视频的解码)。

这使用`queue`插件完成，它的sink pad只负责将数据入队，并且在另一个线程中src pad将数据出队并传递给其余插件。这个插件同样可以用来做缓冲机制，这点在后面讲述流的教程中可以看到，`queue`内部队列的长度可以通过属性来设置。

### The example pipeline

![img](images/multithread/basic-tutorial-7.png)

程序的源是合成音频信号(连续的音调)，它被`tee`分离(`tee`将从sink pad中接收到的所有东西通过src pad发送出去)。一个分支将信号传递给声卡，并外一个分支将波形渲染成视频并发送给显示屏。

如上图所示，queue创建了一个新的线程，所以整条pipeline有三个线程。含有多个sink element的pipeline通常是多线程的，因为为了同步多个sink元素通常会互相阻塞直到所有的sink准备好，假如是单线程运行那么它们将被第一个sink阻塞住。

### Request pads

在[Basic tutorail 3: Dynamic pipelines](https://ricardolu.gitbook.io/gstreamer/basic-theory/basic-tutorial-3-dynamic-pipelines)我们了解到`uridecodebin`这个插件在最开始是没有src pad的，直到数据开始传递并且`uridecodebin`知道媒体类型才出现，这类pad被称为`Sometimes Pads`，而通常一直可用的pad被称作`Always Pads`。

还有一类pad是`Request Pad`，这类pad是按需创建的。最典型的例子就是`tee`，它只有sink pad而没有初始化的src pads：它们需要被申请然后`tee`才会添加它们。在这种情况下，一个输入的流可以被复制任意次数。缺点是Request Pad和其他element的连接和sometimes pads一样，需要手动完成。

另外，在PLAYING或PAUSED状态下去申请(或释放)pad需要注意（Pad阻塞，本教程没有讲到这点），在NULL和READY状态去获得pad是安全的。

## Simple multithreaded example

### basic-tutorial-7.c

```c
#include <gst/gst.h>

int main(int argc, char *argv[]) {
  GstElement *pipeline, *audio_source, *tee, *audio_queue, *audio_convert, *audio_resample, *audio_sink;
  GstElement *video_queue, *visual, *video_convert, *video_sink;
  GstBus *bus;
  GstMessage *msg;
  GstPad *tee_audio_pad, *tee_video_pad;
  GstPad *queue_audio_pad, *queue_video_pad;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
  tee = gst_element_factory_make ("tee", "tee");
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
  audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
  audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
  video_queue = gst_element_factory_make ("queue", "video_queue");
  visual = gst_element_factory_make ("wavescope", "visual");
  video_convert = gst_element_factory_make ("videoconvert", "csp");
  video_sink = gst_element_factory_make ("autovideosink", "video_sink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !audio_source || !tee || !audio_queue || !audio_convert || !audio_resample || !audio_sink ||
      !video_queue || !visual || !video_convert || !video_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Configure elements */
  g_object_set (audio_source, "freq", 215.0f, NULL);
  g_object_set (visual, "shader", 0, "style", 1, NULL);

  /* Link all elements that can be automatically linked because they have "Always" pads */
  gst_bin_add_many (GST_BIN (pipeline), audio_source, tee, audio_queue, audio_convert, audio_resample, audio_sink,
      video_queue, visual, video_convert, video_sink, NULL);
  if (gst_element_link_many (audio_source, tee, NULL) != TRUE ||
      gst_element_link_many (audio_queue, audio_convert, audio_resample, audio_sink, NULL) != TRUE ||
      gst_element_link_many (video_queue, visual, video_convert, video_sink, NULL) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Manually link the Tee, which has "Request" pads */
  tee_audio_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for audio branch.\n", gst_pad_get_name (tee_audio_pad));
  queue_audio_pad = gst_element_get_static_pad (audio_queue, "sink");
  tee_video_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
  queue_video_pad = gst_element_get_static_pad (video_queue, "sink");
  if (gst_pad_link (tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
      gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Tee could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }
  gst_object_unref (queue_audio_pad);
  gst_object_unref (queue_video_pad);

  /* Start playing the pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Release the request pads from the Tee, and unref them */
  gst_element_release_request_pad (tee, tee_audio_pad);
  gst_element_release_request_pad (tee, tee_video_pad);
  gst_object_unref (tee_audio_pad);
  gst_object_unref (tee_video_pad);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  return 0;
}
```

## 工作流

```c
/* Create the elements */
audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
tee = gst_element_factory_make ("tee", "tee");
audio_queue = gst_element_factory_make ("queue", "audio_queue");
audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");
video_queue = gst_element_factory_make ("queue", "video_queue");
visual = gst_element_factory_make ("wavescope", "visual");
video_convert = gst_element_factory_make ("videoconvert", "video_convert");
video_sink = gst_element_factory_make ("autovideosink", "video_sink");
```

上述pipeline示例图中的所有elements都在这完成实例化。

`audiotestsrc`生成连续的音调。`wavescope`消费一个音频信号并且将它渲染成音波(可以将它看作一个简易的示波器)。`autoaudiosink`和`autovideosink`在前文介绍过了。

转换element(`audioconvert`，`audioresample`和`videoconvert`)也是必须的，它们可以保证pipeline可以正确地连接。事实上，音频和视频的sink的Caps是由硬件确定的，所以你在设计时是不知道`audiotestsrc`和`wavescope`是否可以匹配上。如果Caps能够匹配，这些element的行为就类似于直通——对信号不做任何修改，这对于性能的影响基本可以忽略不计。

```c
/* Configure elements */
g_object_set (audio_source, "freq", 215.0f, NULL);
g_object_set (visual, "shader", 0, "style", 1, NULL);
```

为了更好的演示做了小小的调整：audiotestsrc的“freq”属性设置成215Hz，wavescope设置“shader”和“style”，让波形连续。用gst-inspect可以更好的了解这几个element的属性。

```c
/* Link all elements that can be automatically linked because they have "Always" pads */
gst_bin_add_many (GST_BIN (pipeline), audio_source, tee, audio_queue, audio_convert, audio_sink,
    video_queue, visual, video_convert, video_sink, NULL);
if (gst_element_link_many (audio_source, tee, NULL) != TRUE ||
    gst_element_link_many (audio_queue, audio_convert, audio_sink, NULL) != TRUE ||
    gst_element_link_many (video_queue, visual, video_convert, video_sink, NULL) != TRUE) {
  g_printerr ("Elements could not be linked.\n");
  gst_object_unref (pipeline);
  return -1;
}
```

这块代码在pipeline里加入了所有的element并且把可以自动连接的element都连接了起来(就是Always Pad)。

注：事实上可以直接使用`gst_element_link_many()`连接`Request Pads`，它会在内部申请Pads所以用户不需要担心连接的elment具有`Always Pads`和`Request Pads`，但这并不方便，因为最终总是要释放申请的Pad而使用`get_element_link_many()`会很容易忽略这点。因此建议的做法是始终手动请求`Request Pads`，避免麻烦。

```c
/* Manually link the Tee, which has "Request" pads */
tee_audio_pad = gst_element_request_pad_simple (tee, "src_%u");
g_print ("Obtained request pad %s for audio branch.\n", gst_pad_get_name (tee_audio_pad));
queue_audio_pad = gst_element_get_static_pad (audio_queue, "sink");
tee_video_pad = gst_element_request_pad_simple (tee, "src_%u");
g_print ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
queue_video_pad = gst_element_get_static_pad (video_queue, "sink");
if (gst_pad_link (tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
    gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
  g_printerr ("Tee could not be linked.\n");
  gst_object_unref (pipeline);
  return -1;
}
gst_object_unref (queue_audio_pad);
gst_object_unref (queue_video_pad);
```

为了连接Request Pad，需要获得对element的申请一个pad。一个element可能可以创建不同种类的Request Pad，所以，当请求Pad生成时，必须提供想要的Pad模板。Pad模板可以`gst_element_class_get_pad_template()`方法来获得，而且用它们的名字来区分开。在tee element的文档里面我们可以看到两个pad模板，分别被称为`sink`(`sink pad`)和`src%_u`(`Request Pad`)。我们使用`gst_element_request_pad()`方法向`tee`请求两个Pad——分别给音频分支和视频分支。

然后我们去获得需要连接`Request Pad`的下游element(`queue`)的`sink Pad`，这些通常都是`Always Pad`，所以我们用`get_element_get_static_pad()`方法去获得。

 最后，我们用`gst_pad_link()`方法把pad连接起来。在`gst_element_link()`和`gst_element_link_many()`方法里面也是调用这个函数来连接的。

我们请求的`queue`的`sink pad`需要通过`gst_object_unref()`来释放。`Request Pad`是在我们不需要的时候释放，也就是在程序的最后。

就像平常一样，我们设置pipeline到`PLAYING`状态，等待一个错误消息或者`EOS`消息到达。剩下的所有事情就是释放请求的Pads。

```c
/* Release the request pads from the Tee, and unref them */
gst_element_release_request_pad (tee, tee_audio_pad);
gst_element_release_request_pad (tee, tee_video_pad);
gst_object_unref (tee_audio_pad);
gst_object_unref (tee_video_pad);
```

`gst_element_release_request_pad()`可以释放`tee`的pad，但还需要调用`gst_object_unref()`减少pad的引用计数(释放)才行。

## 总结

这篇教程展示了：

- 如何使用`queue`在不同线程上运行pipeline的一部分。
- 什么是`Request Pad`以及如何使用`gst_element_request_pad_simple()`，`gst_pad_link()`和`gst_element_release_request_pad()`将elements和`Request Pads`连接。
- 如何使用`tee`复制stream。

下一篇教程将在构建本教程pipeline的基础上展示如何向一条正在运行的pipeline中插入和提取数据。

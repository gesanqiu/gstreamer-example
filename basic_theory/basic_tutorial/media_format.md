# Basic tutorial 6: Media formats and Pad Capabilities

## 目标

Pad的Capabilities时GStreamer的一个基本元素，由于大部分时间都由框架自动处理它们，所以用户很少感觉到它们的存在。这篇略微理论化的教程将展示：

- 什么是Pad Capabilities。
- 如何检索它们。
- 什么时候检索它们。
- 为什么用户需要了解他们。

## 介绍

### Pads

如之前介绍的一般，Pads允许信息进出elements。Pad的Capabilities(简称为Caps)指定了Pad能够传递什么类型的信息。例如，“320x200分辨率，30FPS的RGB视频”，或是“16位音频样本，5.1通道，采样率44100Hz”，或者是mp3和h264这类的压缩格式。

Pads可以支持多种Capabilities(例如一个video sink可以支持不同格式的RGB/YUV视频)并且Capabilities的值可以是一个范围(例如一个audio sink能够支持从1hz到48000hz的采样率)。然而，真正在Pad之间传递的信息必须只有一种明确制定的类型。 通过一个称为“协商”的过程，两个连接的pad就一个公共类型达成一致，从而pad的capability固定下来(它们只有一种类型，不包含范围)。下面的例程讲向你清楚的展现这个协商的过程。

两个elements支持的Capabilities类型必须有交集它们才能连接，否则它们无法理解彼此传递的数据，这就是Capabilities的主要设计目的。

作为一个应用程序开发者，你经常需要通过连接elements来构建piepline，因此你需要了解你所使用的elements的Pad Caps，或者至少当GStreamer elements因为“协商”错误而连接失败能够知道它们具体是什么。

### Pad templates

Pads从Pad Templates生成，Pad Templates指明了一个Pad所有可能的Capabilities。模版对于创建一个相似的Caps时很有用的，并且允许提前拒绝elements之间的连接：假如两个elements的Pad模版的Capabilities没有交集，就没有必要进行更深入的“协商”。

Pad模版可以视作“协商”过程的第一步，随着过程的发展，实际的Pads被实例化并且Pads的Capabilities也不断被完善固定下来(或者“协商“失败)。

### Capabilities examples

```shell
SINK template: 'sink'
  Availability: Always
  Capabilities:
    audio/x-raw
               format: S16LE
                 rate: [ 1, 2147483647 ]
             channels: [ 1, 2 ]
    audio/x-raw
               format: U8
                 rate: [ 1, 2147483647 ]
             channels: [ 1, 2 ]
```

这是一个element的永久`sink pad`(暂时不讨论`Availablility`)。它支持2种媒体格式，都是音频的原始数据`audio/x-raw-int`，16位的小端序符号数和8位的无符号数。方括号表示一个范围，例如，频道`channels`的范围是1到2.

```shell
SRC template: 'src'
  Availability: Always
  Capabilities:
    video/x-raw
                width: [ 1, 2147483647 ]
               height: [ 1, 2147483647 ]
            framerate: [ 0/1, 2147483647/1 ]
               format: { I420, NV12, NV21, YV12, YUY2, Y42B, Y444, YUV9, YVU9, Y41B, Y800, Y8, GREY, Y16 , UYVY, YVYU, IYU1, v308, AYUV, A420 }
```

`video/x-raw`表示这个`source pad`输出原始格式的视频。它支持一个很广的维数和帧率，一系列的YUV格式(用花括号列出了)。所有这些格式都显示不同的图像编码格式和子采样程度。

### 注解

用户可以使用`gst-inspect-1.0`工具学习所有GStreamer element是Caps信息。

注意有些elements需要查询底层硬件以获得支持的格式，并相应地提供它们的Pad Caps(通常在element的READY状态或者更早)。因此同一个element在不同平台上支持的Caps有可能不同，甚至某两次运行之间就会有所不同(虽然这种情况很少见)。

这篇教程实例化了两个elements(通过GstElementFactory的方式)，展示了他们的Pad模版，连接它们并将pipeline设置为播放状态。在每个状态变化的阶段，展示了sink element的Pad的Capabilities，你能够观察到在整个“协商”过程中Pad Caps固定之前的所有变化。

## A trivial Pad Capabilities Example

### basic-tutorial-6.c

```c
#include <gst/gst.h>

/* Functions below print the Capabilities in a human-friendly format */
static gboolean print_field (GQuark field, const GValue * value, gpointer pfx) {
  gchar *str = gst_value_serialize (value);

  g_print ("%s  %15s: %s\n", (gchar *) pfx, g_quark_to_string (field), str);
  g_free (str);
  return TRUE;
}

static void print_caps (const GstCaps * caps, const gchar * pfx) {
  guint i;

  g_return_if_fail (caps != NULL);

  if (gst_caps_is_any (caps)) {
    g_print ("%sANY\n", pfx);
    return;
  }
  if (gst_caps_is_empty (caps)) {
    g_print ("%sEMPTY\n", pfx);
    return;
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);

    g_print ("%s%s\n", pfx, gst_structure_get_name (structure));
    gst_structure_foreach (structure, print_field, (gpointer) pfx);
  }
}

/* Prints information about a Pad Template, including its Capabilities */
static void print_pad_templates_information (GstElementFactory * factory) {
  const GList *pads;
  GstStaticPadTemplate *padtemplate;

  g_print ("Pad Templates for %s:\n", gst_element_factory_get_longname (factory));
  if (!gst_element_factory_get_num_pad_templates (factory)) {
    g_print ("  none\n");
    return;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = pads->data;
    pads = g_list_next (pads);

    if (padtemplate->direction == GST_PAD_SRC)
      g_print ("  SRC template: '%s'\n", padtemplate->name_template);
    else if (padtemplate->direction == GST_PAD_SINK)
      g_print ("  SINK template: '%s'\n", padtemplate->name_template);
    else
      g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

    if (padtemplate->presence == GST_PAD_ALWAYS)
      g_print ("    Availability: Always\n");
    else if (padtemplate->presence == GST_PAD_SOMETIMES)
      g_print ("    Availability: Sometimes\n");
    else if (padtemplate->presence == GST_PAD_REQUEST)
      g_print ("    Availability: On request\n");
    else
      g_print ("    Availability: UNKNOWN!!!\n");

    if (padtemplate->static_caps.string) {
      GstCaps *caps;
      g_print ("    Capabilities:\n");
      caps = gst_static_caps_get (&padtemplate->static_caps);
      print_caps (caps, "      ");
      gst_caps_unref (caps);

    }

    g_print ("\n");
  }
}

/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}

int main(int argc, char *argv[]) {
  GstElement *pipeline, *source, *sink;
  GstElementFactory *source_factory, *sink_factory;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the element factories */
  source_factory = gst_element_factory_find ("audiotestsrc");
  sink_factory = gst_element_factory_find ("autoaudiosink");
  if (!source_factory || !sink_factory) {
    g_printerr ("Not all element factories could be created.\n");
    return -1;
  }

  /* Print information about the pad templates of these factories */
  print_pad_templates_information (source_factory);
  print_pad_templates_information (sink_factory);

  /* Ask the factories to instantiate actual elements */
  source = gst_element_factory_create (source_factory, "source");
  sink = gst_element_factory_create (sink_factory, "sink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline */
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Print initial negotiated caps (in NULL state) */
  g_print ("In NULL state:\n");
  print_pad_capabilities (sink, "sink");

  /* Start playing */
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state (check the bus for error messages).\n");
  }

  /* Wait until error, EOS or State Change */
  bus = gst_element_get_bus (pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS |
        GST_MESSAGE_STATE_CHANGED);

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
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
            g_print ("\nPipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
            /* Print the current capabilities of the sink element */
            print_pad_capabilities (sink, "sink");
          }
          break;
        default:
          /* We should not reach here because we only asked for ERRORs, EOS and STATE_CHANGED */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (source_factory);
  gst_object_unref (sink_factory);
  return 0;
}
```

## 工作流

`print_pad_capabilities`， `print_caps`，`print_pad_templates`以一种友好的形式简单展示了capabilities的结构体。假如你想了解GstCaps的内部结构，请阅读[GstCaps]()。

```c
/* Shows the CURRENT capabilities of the requested pad in the given element */
static void print_pad_capabilities (GstElement *element, gchar *pad_name) {
  GstPad *pad = NULL;
  GstCaps *caps = NULL;

  /* Retrieve pad */
  pad = gst_element_get_static_pad (element, pad_name);
  if (!pad) {
    g_printerr ("Could not retrieve pad '%s'\n", pad_name);
    return;
  }

  /* Retrieve negotiated caps (or acceptable caps if negotiation is not finished yet) */
  caps = gst_pad_get_current_caps (pad);
  if (!caps)
    caps = gst_pad_query_caps (pad, NULL);

  /* Print and free */
  g_print ("Caps for the %s pad:\n", pad_name);
  print_caps (caps, "      ");
  gst_caps_unref (caps);
  gst_object_unref (pad);
}
```

`gst_element_get_static_pad()`用于根据Pad name检索给定element的pad结构体，这个pad是静态的，因为它会一直存在。关于Pad的的更多内容请阅读[GstPad]()。

获取pad之后我们调用`gst_pad_get_current_caps()`来获取这个pad当前的capabilities，可能是固定的也可能不是，这取决于当前“协商”过程的状态。pad甚至可能还未生成capabilities，在这种情况下，我们调用`gst_pad_query_caps()`来获取一个当前可接受的Pad Capabilities。这个当前可接受的Caps是Pad Template在`NULL`状态下的Caps，它不是固定的，因为还会查询实际的硬件。

然后我们打印这些获得的Capabilities信息。

```c
/* Create the element factories */
source_factory = gst_element_factory_find ("audiotestsrc");
sink_factory = gst_element_factory_find ("autoaudiosink");
if (!source_factory || !sink_factory) {
  g_printerr ("Not all element factories could be created.\n");
  return -1;
}

/* Print information about the pad templates of these factories */
print_pad_templates_information (source_factory);
print_pad_templates_information (sink_factory);

/* Ask the factories to instantiate actual elements */
source = gst_element_factory_create (source_factory, "source");
sink = gst_element_factory_create (sink_factory, "sink");
```

在之前的教程中我们使用`gst_element_factory_make()`来创建GStreamer element并且跳过了factories的讨论，可以明确的是一个`GstElementFactory`管理着一个特定类型的GStreamer element的实例化，以factory name区分(可以理解为一个`GstElementFactory`代表一个插件，一个插件可以实例化多个GStreamer element对象)。

`gst_element_factory_make()`是`gst_element_factory_create()`和`gst_element_factory_create()`的简洁形式。

通过工程，Pad模板实际上已经可以访问了，所以factories一建立我们立刻打印这些信息。

我们跳过pipeline的创建和启动部分，直接跳到状态切换消息的处理：

```c
case GST_MESSAGE_STATE_CHANGED:
  /* We are only interested in state-changed messages from the pipeline */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    g_print ("\nPipeline state changed from %s to %s:\n",
        gst_element_state_get_name (old_state), gst_element_state_get_name (new_state));
    /* Print the current capabilities of the sink element */
    print_pad_capabilities (sink, "sink");
  }
  break;
```

上述代码将在每次pipeline状态变化时打印`autoaudiosink`的`sink pad`。在输出中你能看到一个最初的caps (Pad Template的caps)是如何逐步完善的知道它们完全固定(Caps只包含一个无范围的类型)。

## 总结

这篇教程展示了：

- 什么是`Pad Capabilities`和`Pad Template Capabilities`。
- 如何使用`gst_pad_get_current_caps()`和`get_pad_query_caps()`检索它们。
- 它们根据pipeline的不同状态有不同的含义(在初始化时它们表示所有可能的Capabilities，在这之后表示当前Pad的特定Caps)。
- 事先知道elements支持的Caps类型对于elements的连接至关重要。
- 可以使用`gst_inspect-1.0`查看element支持的Pad Caps。
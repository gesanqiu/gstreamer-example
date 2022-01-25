# Building an Application

这一章节，我们将讨论GStreamer的基础概念和最常用的objects，例如elements，pads和buffers。我们将使用这些objects的可视化表示，以便能够可视化您稍后将学习构建的更复杂的pipeline。你将对GStreamer的API有一个初步印象，足以构建基本的应用。之后你将学着构建一个基础的命令行应用。

## Initializing GStreamer

在开发GStreamer应用的过程中，可以简单的只包含`gst/gst.h`即可访问大部分库函数，除了某些特殊plugin的定制化API需要单独包含头文件。

此外，为了能够调用GStreamer库，在主程序中必须调用`gst_init(&argc, &argv)`函数完成必须的初始化工作以及命令行参数的分析。

## Element

GStreamer应用中最重要的对象是GstElement对象，element是多媒体pipeline基本的构建组件，所有的高级组件都集成自GstElement。

Gstreamer中主要有三种elements：sink element，src element，filter-like element，element的类型由其具备哪些pads决定（pad相关内容将在后续小节展开介绍）。

![Visualisation of a source element](images/src-element.png)

![Visualisation of a filter element](images/filter-element.png)

![Visualisation of a sink element](images/sink-element.png)

### Creating a GstElement

```c++
gst_init (&argc, &argv);

GstElement* element;
element = gst_element_factory_make("factory name", "unique element name");
// or
GstElementFactory* factory;
GstElement* element;
factory = gst_element_factory_find ("factory name");
element = gst_element_factory_create (factory, "unique element name");
```

### Using an element as a GObject

一个GstElement能够拥有一些属性，这些属性是使用标准GObject的属性实现的。每个GstElement至少继承了GObject的“name”属性，也即创建GstElement时所传递的“unique element name”。GObject为common属性提供了setter和getter，但更一般的做法是使用`g_object_set/get`。

```c++
g_object_set(G_OBJECT(element), "propeerty name", &value, NULL);
g_object_get(G_OBJECT(element), "propeerty name", &value, NULL);
```

在属性的查询上，GStreamer提供了`gst-inspect-1.0`工具用于查询指定element的属性和简单描述。

除了属性，通常一个GstElement还提供一些GObject信号以实现灵活的**回调机制**用于pipeline和application交互。

### More about element factories

Element factories是GStreamer注册表中检索的基本单位，他们描述了所有GStreamer能够创建的插件和elements。

从上面创建GstElement对象的过程来看，可以理解为我们从一个GstElementFactory中创建了一个GstElemnt对象，但是我对于GstElementFactory的理解很粗糙，官方文档也没有详细的设计说明。这个GstElementFactory和工厂模式不太一样，在GStreamer中一个plugin就是一个工厂，或许是因为一个plugin可能由多个elements实现。

GstElementFactory最重要的特性就是它拥有其所属于的plugin所支持的pads的完整描述，而不需要将element真正加载进内存。

### Link Elements

两个elements的连接，实际是src-pad和sink-pad之间的negotiation，因此连接需要两个pad的caps具备交集以及elements处于同一个GstBin。

```c++
gst_element_link();
gst_elemenet_link_many();
gst_element_link_pads();
```

### Element States

GStreamer的elements仅有四种状态，四种状态从NULL<->READY<->PAUSE<->PLAYING必须依次切换，即使越级切换状态成功也是接口内部完成了相关的操作。

`GST_STATE_NULL`：默认状态，在这个状态不会申请任何资源，当element的引用计数变为0时必须处于NULL状态。其他状态切换到这个状态会释放掉所有已申请的资源。

`GST_STATE_READY`：在这个状态下element会申请相关的全局资源，但不涉及stream数据。简单来说就是NULL->READY仅是打开相关的硬件设备，申请buffer；PLAYING->READY就是把停止读取stream数据。

`GST_STATE_PAUSE`：这个状态实际是GStreamer中最常见的一个状态，在这个阶段pipeline打开了stream但并未处理它，例如sink element已经读取到了视频文件的第一帧并准备播放。

`GST_STATE_PLAYING`：PLAYING状态和PAUSE状态实际并没有区别，只是PLAYING允许clock 润run。

通常只需要设置bin或者pipeline元素的状态即可自动完成其内含的所有elements的状态切换，但假如动态的向一条处于PLAYING状态的pipeline添加element，则需要手动完成这个element的状态切换。

## Bin

GstBin可以将一系列elements组合形成一个逻辑上的element，以便从整体上操控和管理elements。

- 最外层的bin即使pipeline。
- GstBin管理它内部elements的状态。

## Bus

GstBus是将stream线程消息转发给应用程序线程的系统。

- GstBus本身运行在应用程序的上下文中，但能够自动监听GStreamer内的线程。
- 每条pipeline都自带一条GstBus，开发人员仅需为其设定handler以便在接收到消息是能或者正确的处理。

```c++
#include <gst/gst.h>

static GMainLoop *loop;

static gboolean
my_bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  g_print ("Got %s message\n", GST_MESSAGE_TYPE_NAME (message));

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:{
      GError *err;
      gchar *debug;

      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s\n", err->message);
      g_error_free (err);
      g_free (debug);

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      /* end-of-stream */
      g_main_loop_quit (loop);
      break;
    default:
      /* unhandled message */
      break;
  }

  /* we want to be notified again the next time there is a message
   * on the bus, so returning TRUE (FALSE means we want to stop watching
   * for messages on the bus and our callback should not be called again)
   */
  return TRUE;
}

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstBus *bus;
  guint bus_watch_id;

  /* init */
  gst_init (&argc, &argv);

  /* create pipeline, add handler */
  pipeline = gst_pipeline_new ("my_pipeline");

  /* adds a watch for new message on our pipeline's message bus to
   * the default GLib main context, which is the main context that our
   * GLib main loop is attached to below
   */
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  bus_watch_id = gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_object_unref (bus);

  /* [...] */

  /* create a mainloop that runs/iterates the default GLib main context
   * (context NULL), in other words: makes the context check if anything
   * it watches for has happened. When a message has been posted on the
   * bus, the default main context will automatically call our
   * my_bus_callback() function to notify us of that message.
   * The main loop will be run until someone calls g_main_loop_quit()
   */
  loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (loop);

  /* clean up */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  g_source_remove (bus_watch_id);
  g_main_loop_unref (loop);

  return 0;
}

```

loop线程（主线程）会定期检查它所监听的消息是否发生。

- GstBus的消息监听是异步的，无法处理同步需求。
- `gst_bus_add_watch()`会处理所有类型的GstBus消息，可以在handler中使用switch语句进行细化，也可以用`gst_bus_add_signal_watch(bus)`和`g_signal_connect(bus, "message::eos", G_CALLBACK(cb_message_eos), NULL)`来为特定类型的消息创建一个handler。

## Pads and Capabilities

Pad是一个element与外部交互的接口，数据从一个element的src-pad传递给另一个element的sink-pad。Pad的Capabilities表明element能处理的数据。

### Dynamic(or sometimes) pads

- 某些elements在创建时并不会带有pads，例如demuxer，demuxer在创建时并不带有sink-pad，直到pipeline处于PAUSE状态读取到了stream的足够信息。demuxer将解析读取的stream中的所有基本流(Video，Audio)数据并为它们分别创建一个能处理对应流数据类型的cap的pad。
- `pad-added`：对于具有sometimes pad的element，它将在创建一个新的pad的时候发出一个signal，用户需要对这个信号做一定的处理以便能正常的连接pipeline中的elemnet。

```C++
static void cb_new_pad(GstElement* element, GstPad* pad, gpointer data);
g_signal_connect(demux, "pad-added", G_CALLBACK(cb_new_pad), NULL);
```

### Request pads

request pads是根据请求才创建的pad，广泛应用于muxer，aggregator，tee中，大多数情况下用户不需要处理request pads。

```c++
gst_element_request_pad_simple(tee, "src_%d");
gst_element_get_compatiable_pad(mux, tolink_pad, NULL);
```

### Capabilities of a pad

Capabilities是用于描述一个pad能够处理或正在处理的数据类型的机制。GStreamer使用GstCaps描述pads的capabilities，一个GstCaps将含有一个或多个GStructure来描述媒体类型，但对于已经完成negotiation的pad，其GstCaps的GStructure是唯一的，并且属性值是固定的。

### What capabilities are used for

- Autoplugging：基于pad的的caps自动查找能与其link的element；
- Compatibility detection：为pad negotiation提供支持；
- Metadata：读取pad的caps，可以或缺当前正在播放的流 的信息；
- Filtering：用于限制两个pad之间支持的流类型，常被置于convert elements之后，用于指定上一个与其连接的elements的输出数据格式。

### Using capabilities for metadata

```c++
const GStructure* structure;

structure = gst_caps_get_structure(caps, 0);
gst_structure_get_int(structure, "width", &width);
```

### Creating capabilities of filtering

```c++
GstCaps* caps;
gboolean link_ok;

caps = gst_caps_new_simple ("video/x-raw",
                             "format", G_TYPE_STRING, "I420",
                             "width", G_TYPE_INT, 384,
                             "height", G_TYPE_INT, 288,
                             "framerate", GST_TYPE_FRACTION, 25, 1,
                             NULL);

link_ok = gst_element_link_filtered (element1, element2, caps);
gst_caps_unref (caps);
// or
GstElement* capfilter;

capfilter = gst_element_factory_make("capfilter", "capfilter0");
g_object_set(G_OBJECT(capfilter), "caps", caps, NULL);
gst_bin_add_many(GST_BIN(pipeline), capfilter, NULL);
gst_element_link_many(elem1, capfilter, elem2, NULL);
```

`gst_element_link_filtered()`内部会自动根据caps创建一个capfilter element并将其插在两个待链接的元素之间。

# Autoplugging

由于autoplugging这一概念具备充分的动态性，GStreamer可以自动拓展以支持新的数据类型而无需修改autoplugger。

## Media types as a way to identify streams

Auto plugging的核心问题是如何使用media type作为一种动态和可拓展的识别stream的方式，在之前的文章中有提到pad的capabilities是elements交互协商的一种机制，一个capability是一种media type和一系列properties的组合。

## Media steam type detection

Auto plugging在具体是线上依赖于media type detection，GStreamer的pipeline自带的typefinding机制负责实现这部分功能，当stream进入到pipeline中插件，只要stream type是未知的，就会一直读data。

- 在typefinding阶段，它会将所有实现了typefinder的plugins提供数据，当有一个typefinder识别了这个流（能够处理这种media type的数据），那么typefinder将发出一个信号。假如数据没有被任何一个plugin识别，那么进一步的media处理将停止（程序终止运行）。
- 实现了typefinding功能的plugin需要上报自身能够处理的media type和通常封装这种media type的文件格式，以及一个typefind函数。
- GStreamer提供了一个typefind插件，用户可以依赖他完成自己的auto plugging过程。当`typefind()`被调用，plugin将查看data的media type是否与已上报的media type是否匹配，如果匹配成功，plugin将会通知typefind element它识别出的media type以及置信度。当整个typefinding过程结束，假如有plugin识别了data，那么typefind element将出发`have-type`信号，否则报错。

```c++
#include <gst/gst.h>

[.. my_bus_callback goes here ..]

static gboolean
idle_exit_loop (gpointer data)
{
  g_main_loop_quit ((GMainLoop *) data);

  /* once */
  return FALSE;
}

static void
cb_typefound (GstElement *typefind,
          guint       probability,
          GstCaps    *caps,
          gpointer    data)
{
  GMainLoop *loop = data;
  gchar *type;

  type = gst_caps_to_string (caps);
  g_print ("Media type %s found, probability %d%%\n", type, probability);
  /*
  if (strcmp(type, "video/x-h264") == 0) {
      // link element
      gst_element_link_many(typefind, data->decoder, NULL);
  }
  */
  g_free (type);

  /* since we connect to a signal in the pipeline thread context, we need
   * to set an idle handler to exit the main loop in the mainloop context.
   * Normally, your app should not need to worry about such things. */
  g_idle_add (idle_exit_loop, loop);
}

gint
main (gint   argc,
      gchar *argv[])
{
  GMainLoop *loop;
  GstElement *pipeline, *filesrc, *typefind, *fakesink;
  GstBus *bus;

  /* init GStreamer */
  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  /* check args */
  if (argc != 2) {
    g_print ("Usage: %s <filename>\n", argv[0]);
    return -1;
  }

  /* create a new pipeline to hold the elements */
  pipeline = gst_pipeline_new ("pipe");

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_add_watch (bus, my_bus_callback, NULL);
  gst_object_unref (bus);

  /* create file source and typefind element */
  filesrc = gst_element_factory_make ("filesrc", "source");
  g_object_set (G_OBJECT (filesrc), "location", argv[1], NULL);
  typefind = gst_element_factory_make ("typefind", "typefinder");
  g_signal_connect (typefind, "have-type", G_CALLBACK (cb_typefound), loop);
  fakesink = gst_element_factory_make ("fakesink", "sink");

  /* setup */
  gst_bin_add_many (GST_BIN (pipeline), filesrc, typefind, fakesink, NULL);
  gst_element_link_many (filesrc, typefind, fakesink, NULL);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* unset */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (GST_OBJECT (pipeline));

  return 0;
}
```


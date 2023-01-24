# Metadata

GStreamer对其支持的2种元数据有着清晰的分类。其一，Stream-tag，这类元数据以非技术的方式描述数据流中的内容。其二，Stream-info，它则以技术方式准确描述数据流中的各项属性。 例如，当数据流为一首MV歌曲时，Stream-tag的内容可以为歌曲作者，歌曲名称和唱片信息，Stream-info的内容则为视频大小，音频采样频率，编码方式等。 

GStreamer使用基于bus的标签系统处理Stream-tag类型的元数据。而Stream-info元数据则是通过Pad之间协商Capabilities时来传递。

## Metadata reading

如前所述，获取Stream-info元数据最便利的方式是从一个GstPad实例中读取。注意，这种方式需要应用程序对所有需要读取的Pad有访问权限。由于在《Using capabilities for metadata》一文中已对此方式展开讲解，这里不再重复。 

Stream-tag元数据的读取使用的是GStreamer中的bus。应用程序监听GST_MESSAGE_TAG类型的消息并从中处理即可。基本操作也已涵盖在《Bus》一节中。 

但是，需要特别注意的是，GST_MESSAGE_TAG消息可能会被产生多次，因此应用程序需要负责以连贯的方式对它们进行合并和显示。这可以通过gst_tag_list_merge()函数完成。不过在某些实际场景中，请确保信息及时更新，比如在新加载一首歌曲时或持续几分钟接收互联网广播后，应用都需要清空缓存空间。并且，如需令后出现的Stream-tag元数据能成功替换已有元数据，确保在合并时使用GST_TAG_MERGE_PREPEND模式。 

下面的示例演示如何从文件中提取标签并将标签打印至控制台。

```c
/* compile with:
 * gcc -o tags tags.c `pkg-config --cflags --libs gstreamer-1.0` */
#include <gst/gst.h>

static void
print_one_tag (const GstTagList * list, const gchar * tag, gpointer user_data)
{
  int i, num;

  num = gst_tag_list_get_tag_size (list, tag);
  for (i = 0; i < num; ++i) {
    const GValue *val;

    /* Note: when looking for specific tags, use the gst_tag_list_get_xyz() API,
     * we only use the GValue approach here because it is more generic */
    val = gst_tag_list_get_value_index (list, tag, i);
    if (G_VALUE_HOLDS_STRING (val)) {
      g_print ("\t%20s : %s\n", tag, g_value_get_string (val));
    } else if (G_VALUE_HOLDS_UINT (val)) {
      g_print ("\t%20s : %u\n", tag, g_value_get_uint (val));
    } else if (G_VALUE_HOLDS_DOUBLE (val)) {
      g_print ("\t%20s : %g\n", tag, g_value_get_double (val));
    } else if (G_VALUE_HOLDS_BOOLEAN (val)) {
      g_print ("\t%20s : %s\n", tag,
          (g_value_get_boolean (val)) ? "true" : "false");
    } else if (GST_VALUE_HOLDS_BUFFER (val)) {
      GstBuffer *buf = gst_value_get_buffer (val);
      guint buffer_size = gst_buffer_get_size (buf);

      g_print ("\t%20s : buffer of size %u\n", tag, buffer_size);
    } else if (GST_VALUE_HOLDS_DATE_TIME (val)) {
      GstDateTime *dt = g_value_get_boxed (val);
      gchar *dt_str = gst_date_time_to_iso8601_string (dt);

      g_print ("\t%20s : %s\n", tag, dt_str);
      g_free (dt_str);
    } else {
      g_print ("\t%20s : tag of type '%s'\n", tag, G_VALUE_TYPE_NAME (val));
    }
  }
}

static void
on_new_pad (GstElement * dec, GstPad * pad, GstElement * fakesink)
{
  GstPad *sinkpad;

  sinkpad = gst_element_get_static_pad (fakesink, "sink");
  if (!gst_pad_is_linked (sinkpad)) {
    if (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK)
      g_error ("Failed to link pads!");
  }
  gst_object_unref (sinkpad);
}

int
main (int argc, char ** argv)
{
  GstElement *pipe, *dec, *sink;
  GstMessage *msg;
  gchar *uri;

  gst_init (&argc, &argv);

  if (argc < 2)
    g_error ("Usage: %s FILE or URI", argv[0]);

  if (gst_uri_is_valid (argv[1])) {
    uri = g_strdup (argv[1]);
  } else {
    uri = gst_filename_to_uri (argv[1], NULL);
  }

  pipe = gst_pipeline_new ("pipeline");

  dec = gst_element_factory_make ("uridecodebin", NULL);
  g_object_set (dec, "uri", uri, NULL);
  gst_bin_add (GST_BIN (pipe), dec);

  sink = gst_element_factory_make ("fakesink", NULL);
  gst_bin_add (GST_BIN (pipe), sink);

  g_signal_connect (dec, "pad-added", G_CALLBACK (on_new_pad), sink);

  gst_element_set_state (pipe, GST_STATE_PAUSED);

  while (TRUE) {
    GstTagList *tags = NULL;

    msg = gst_bus_timed_pop_filtered (GST_ELEMENT_BUS (pipe),
        GST_CLOCK_TIME_NONE,
        GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_TAG | GST_MESSAGE_ERROR);

    if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_TAG) /* error or async_done */
      break;

    gst_message_parse_tag (msg, &tags);

    g_print ("Got tags from element %s:\n", GST_OBJECT_NAME (msg->src));
    gst_tag_list_foreach (tags, print_one_tag, NULL);
    g_print ("\n");
    gst_tag_list_unref (tags);

    gst_message_unref (msg);
  }

  if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;

    gst_message_parse_error (msg, &err, NULL);
    g_printerr ("Got error: %s\n", err->message);
    g_error_free (err);
  }

  gst_message_unref (msg);
  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (pipe);
  g_free (uri);
  return 0;
}

```

## Tag writing

应用程序通过GstTagSetter接口写入Stream-tag元数据。所有支持tag-set的element都可以完成 Stream-tag元数据的写入。 

为了找到流水线中支持tag-set的element，应用可以通过gst_bin_iterate_all_by_interface (pipeline, GST_TYPE_TAG_SETTER)函数进行检查。对于最终产生Stream-tag元数据的组件，这样的element常见的有编码器和复用器，应用可以调用gst_tag_setter_merge_tags ()函数将一个Stream-tag元数据列表设置为该element的元数据，或使用gst_tag_setter_add_tags ()函数来为element设置一系列独立的Stream-tag。 

还有一个有用的Stream-tag特性是流水线中所有Stream-tag都会被保存。这意味着当应用程序在转码一个含有Stream-tag的文件为另一种支持Stream-tag的媒体格式时，原始Stream-tag会被认为是数据流的一部分，从而被自然合并到新媒体格式的文件中。

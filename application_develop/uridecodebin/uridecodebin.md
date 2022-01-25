# uridecodebin

`uridecodebin`能够将URI数据解码成raw media，它会自动选择一个能处理uri数据的source element并将其和`decodebin`链接。

**Github: [uridecodebin](https://github.com/gesanqiu/gstreamer-example/tree/main/application_develop/uridecodebin)**

## uri

```c++
bool Create (void)
{
    // ...

    g_object_set (G_OBJECT (m_source), "uri", m_config.src.c_str(), NULL);

    // ...
}
```

## signals

### source-setup

```c++
static void cb_uridecodebin_source_setup (
    GstElement* pipeline, GstElement* source, gpointer user_data)
{
    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    LOG_INFO_MSG ("cb_uridecodebin_source_setup called");

    /* Configure rtspsrc
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "latency")) {
        LOG_INFO_MSG ("cb_uridecodebin_source_setup set %d latency",
            vp->m_config.rtsp_latency);
        g_object_set (G_OBJECT (source), "latency", vp->m_config.rtsp_latency, NULL);
    }
    */

   /* Configure appsrc
    GstCaps *m_sCaps;
    src_Caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.src_format.c_str(), "width", G_TYPE_INT, m_config.src_width,
          "height", G_TYPE_INT, m_config.src_height, NULL);
    g_object_set (G_OBJECT(source), "caps", src_Caps, NULL);
    g_signal_connect (source, "need-data", G_CALLBACK (start_feed), data);
    g_signal_connect (source, "enough-data", G_CALLBACK (stop_feed), data);
    gst_caps_unref (src_Caps);
   */
}
```

`uridecodebin`会分析`uri`属性的值，然后选择合适的`srouce element`，这个`uri`值必须是完整的绝对路径，由source类型开始。

### child-added

```c++
static void cb_decodebin_child_added (
    GstChildProxy* child_proxy, GObject* object, gchar* name, gpointer user_data)
{
    LOG_INFO_MSG ("cb_decodebin_child_added called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    LOG_INFO_MSG ("Element '%s' added to decodebin", name);

    if (g_strrstr (name, "qtdemux") == name) {
        vp->m_qtdemux = reinterpret_cast<GstElement*> (object);
    }

    if ((g_strrstr (name, "h264parse") == name)) {
        vp->m_h264parse = reinterpret_cast<GstElement*> (object);
    }

    if (g_strrstr (name, "qtivdec") == name) {
        vp->m_decoder = reinterpret_cast<GstElement*> (object);
        g_object_set (object, "turbo", vp->m_config.turbo, NULL);
        g_object_set (object, "skip-frames", vp->m_config.skip_frame, NULL);
    }
}

static void cb_uridecodebin_child_added (
    GstChildProxy* child_proxy, GObject* object, gchar* name, gpointer user_data)
{
    LOG_INFO_MSG ("cb_uridecodebin_child_added called");

    LOG_INFO_MSG ("Element '%s' added to uridecodebin", name);
    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    if (g_strrstr (name, "decodebin") == name) {
        g_signal_connect (G_OBJECT (object), "child-added",
            G_CALLBACK (cb_decodebin_child_added), vp);
    }
}
```

通过打印log可以看出`uridecodebin`初始化过程中自动添加到pipeline中的所有GstElement：

```
 filesrc: qtdemux, multiqueue, h264parse/h265parse, capfilter, aacparse, avdec_aac, qtivdec
 rtspsrc: rtph264depay/rtph265depay, h264parse/h265parse, capfilter, qtivdec
```

在`uridecodebin`只会添加`decodebin`一个GstElement，上述的GstElement均由`decodebin`构建，因此除了`uridecodebin`的`child-added`回调，还在其回调中添加了一个`decodebin`的`child-added`回调，用于设置`decodebin`构建的GstElement的属性。

在[build pipeline](https://ricardolu.gitbook.io/gstreamer/application-development/build-pipeline)中提到关于`filesrc`插件的解复用，需要手动进行link，但这个link在实际测试过程中有`decodebin`自动完成，我尝试手动去做link，由于无法获取到`h264parse`的`sink pad`程序抛出了段错误。我打印了这时候`vp->m_h264parse`的指针地址，是一个初始值，说明这时候还未初始化`vp->m_h264parse`。

### pad-added

```c++
static void cb_uridecodebin_pad_added (
    GstElement* src, GstPad* new_pad, gpointer user_data)
{
    LOG_INFO_MSG ("cb_uridecodebin_pad_added called");

    GstPadLinkReturn ret;
    GstCaps*         new_pad_caps = NULL;
    GstStructure*    new_pad_struct = NULL;
    const gchar*     new_pad_type = NULL;
    GstPad*          v_sinkpad = NULL;
    GstPad*          a_sinkpad = NULL;

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);

    if (g_str_has_prefix (new_pad_type, "video/x-raw")) {
        LOG_INFO_MSG ("Linking video/x-raw");
        /* Attempt the link */
        v_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_videoConv), "sink");
        ret = gst_pad_link (new_pad, v_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link video source with waylandsink");
            goto exit;
        }
    } else if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        LOG_INFO_MSG ("Linking audio/x-raw");
        a_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_audioConv), "sink");
        ret = gst_pad_link (new_pad, a_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link audio source and audioconvert");
            goto exit;
        }
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    /* Unreference the sink pad */
    if (v_sinkpad) gst_object_unref (v_sinkpad);
    if (a_sinkpad) gst_object_unref (a_sinkpad);
}
```

将`uridecodebin`的`src pad`和`waylandsin`的`sink pad`连接起来，由于这时是解码后的数据，因此`caps`为`video/x-raw`或`audio/x-raw`类型。

**在上文中提到关于`qtdemux`解复用器的链接问题，事实上`uridecodebin`内部会自动处理好这部分链接，并且为所有能够解析的数据类型创建`src-pad`，每创建一种类型的`src-pad`，`pad-added`回调就会触发一次，用户需要自行完成`uridecodebin`的这些`src-pad`与后续GstElement的链接。**可以结合[Build Pipeline](https://ricardolu.gitbook.io/gstreamer/application-development/build-pipeline#gst_element_link_pads)中关于pad-link的内容一起理解。

在上述代码中我将`video/x-raw`类型的数据和`videoconvert`插件链接，将`audio/x-raw`与`audioconvert`链接，`videoconvert`和`audioconvert`几乎是万能的格式转换插件能够提高代码的可移植性，使得代码能够在各种平台上正常运行，但切记这两者的转换均是使用CPU完成，因此十分消耗性能，在推流中使用会造成极大的延迟。


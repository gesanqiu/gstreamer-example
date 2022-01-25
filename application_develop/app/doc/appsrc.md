# Appsrc

应用程序可以通过Appsrc插件向GStreamer pipeline中插入数据，与大部分插件不同，除了Action signals的方式以外，appsrc还提供了一系列的外部接口`gst_app_src_<function_name>()`用于数据交互以及appsrc属性的动态设置(需要链接`libgstapp.so`)。

## Properties

#### emit-signals

appsrc的`emit-signals`属性默认为true。

### caps

除非push的buffer具有未知的caps，使用appsrc：

- 需要设置caps属性，指定我们会产生何种类型的数据，这样GStreamer会在连接阶段检查后续的Element是否支持此数据类型，否则回调只触发一次就被block在其他插件中。这里的 caps必须为GstCaps对象。
- 使用`gst_app_src_push_sample()`直接push sample，然后接口将接管这个GstSample的控制权(自动释放)，并获取这个GstSample的GstCaps作为caps。

### max-buffers/max-bytes/max-time & block

appsrc内部维护一个数据队列，`max-buffers/max-bytes/max-time`这几个属性用于控制这个内部队列的长度。一个填满的队列将发送`enough-data`信号，这时应用程序应该停止向队列push data。

假如`block`属性设置为true，当内部队列为满时将block push-buffer相关方法直到队列不满。

### stream-type

### is-live

## signals

- `enough-data `：appsrc内部数据队列满，推荐在触发信号之后停止`push-buffer`直到need-data信号被触发。
- `need-data`：appsrc需要更多的数据，在回调或者其他线程中需要调用`push-buffer`或者`end-of-stream`，回调函数的`length`参数是一个隐藏参数，当`length=-1`时意味着appsrc可以接收任意bytes的buffer。可以重复调用`push-buffer`直至`enough-data`信号被触发。
- `seekdata`：需要seekable stream-type的支持，具有一个offset表明下一个要被push的buffer的位置。

### 两种工作模式

- `push-mode`

  push模式由应用程序来控制data的发送，应用程序重复调用`push-buffer/push-sample`方法来触发`enough-data`信号。配合`max-buffers`属性设置队列的长度，通过处理`enough-data`信号和`need-data`信号分别停止或开始调用`push-buffer/push-sample`来控制队列的大小。

- `pull-mode`

  pull模式和通过`need-data`信号触发`push-buffer`调用。

## GST_APP_API

### GstAppSrcCallbacks

```c
typedef struct {
  void      (*need_data)    (GstAppSrc *src, guint length, gpointer user_data);
  void      (*enough_data)  (GstAppSrc *src, gpointer user_data);
  gboolean  (*seek_data)    (GstAppSrc *src, guint64 offset, gpointer user_data);

  /*< private >*/
  gpointer     _gst_reserved[GST_PADDING];
} GstAppSrcCallbacks;
```

### push-buffer

- `gst_app_src_push_buffer`

  ```c
  GstFlowReturn
  gst_app_src_push_buffer (GstAppSrc * appsrc,
                           GstBuffer * buffer)
  ```

  - push-buffer只负责将数据插入appsrc的内部队列中，不负责这个buffer的传输。
  - API将接管这个GstBuffer的所有权，自动释放资源。

### Action signals

- `end-of-stream`：appsrc没有可用的buffer信号

- `push-buffer`：

  ```c
  g_signal_emit_by_name (self, "push-buffer", buffer, user_data, &ret);
  ```

  - 将GstBuffer添加到appsrc的src pad中，不持有这个GstBuffer的所有权，需要手动释放。

- `push-sample`：

  ```c
  g_signal_emit_by_name (self, "push-sample", sample, user_data, &ret);
  ```

  - 将GstSample下的GstBuffer添加到appsrc的src pad中，加入GstSample的GstCaps不符合当前appsrc的cpas，那么将同时把GstSample的GstCpas设置为appsrc的caps属性。不持有这个GstSample的所有权，需要手动释放。

## 代码实例

```c++
// 使用GST_APP_API和Action signal的方式
void CreatePipeline()
{
    // ...

    if (!(m_appsrc = gst_element_factory_make ("appsrc", "appsrc"))) {
        LOG_ERROR_MSG ("Failed to create element appsrc named appsrc");
        goto exit;
    }

    m_transCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.src_format.c_str(), "width", G_TYPE_INT, m_config.src_width,
          "height", G_TYPE_INT, m_config.src_height, NULL);
    // equals to gst_app_src_set_caps (GST_APP_SRC_CAST (m_appsrc), m_transCaps);
    g_object_set (G_OBJECT(m_appsrc), "caps", m_transCaps, NULL);
    gst_caps_unref (m_transCaps); 

    // equals to gst_app_src_set_stream_type (GST_APP_SRC_CAST (m_appsrc),
    //             GST_APP_STREAM_TYPE_STREAM);
    g_object_set (G_OBJECT(m_appsrc), "stream-type",
        GST_APP_STREAM_TYPE_STREAM, NULL);

    g_object_set (G_OBJECT(m_appsrc), "is-live", true, NULL);

    // full definition of appsrc callbacks
    /*
    GstAppSrcCallbacks callbacks = {cb_appsrc_need_data,
                            cb_appsrc_enough_data, cb_appsrc_seek_data};
    gst_app_src_set_callbacks (GST_APP_SRC_CAST (m_appsrc),
        &callbacks, reinterpret_cast<void*> (this), NULL);
    */
    g_signal_connect (m_appsrc, "need-data",
        G_CALLBACK (cb_appsrc_need_data), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_srcPipeline), m_appsrc, NULL);

    // ...
}

GstFlowReturn cb_appsrc_need_data (
    GstElement* appsrc,
    guint length,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_appsrc_need_data called, user_data: %p", user_data);
    SrcPipeline* sp = reinterpret_cast<SrcPipeline*> (user_data);
    GstBuffer* buffer;
    GstMapInfo map;
    GstFlowReturn ret = GST_FLOW_OK;

    std::shared_ptr<cv::Mat> img;

    if (sp->m_getDataFunc) {
        img = sp->m_getDataFunc (sp->m_getDataArgs);

        int len = img->total() * img->elemSize();
        // zero-copy GstBuffer
        // buffer = gst_buffer_new_wrapped(img->data, len);
        buffer = gst_buffer_new_allocate (NULL, len, NULL);

        gst_buffer_map(buffer,&map,GST_MAP_READ);
        memcpy(map.data, img->data, len);

        GST_BUFFER_PTS (buffer) = sp->m_timestamp;
        GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 25);
        sp->m_timestamp += GST_BUFFER_DURATION (buffer) ;

        // equals to gst_app_src_push_buffer (GST_APP_SRC_CAST (appsrc), buffer);
        g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unmap(buffer, &map);
        gst_buffer_unref (buffer);

        if (ret != GST_FLOW_OK) {
        /* something wrong, stop pushing */
        LOG_ERROR_MSG ("push-buffer fail");
        }
    }

    // usleep (25 * 1000);

    return ret;
}
```


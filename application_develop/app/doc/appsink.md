# Appsink

appsink是一个sink插件，具有多种方法允许应用程序获取Pipeline的数据句柄。与大部分插件不同，除了Action signals的方式以外，appsink还提供了一系列的外部接口`gst_app_sink_<function_name>()`用于数据交互以及appsink属性的动态设置(需要链接`libgstapp.so`)。

## Properties

#### emit-signals

appsink的`emit-signals`属性默认为false，假设需要发送`new-preroll`和`new-sample`信号，需要将其设置为true。

### caps

cpas属性用于设置Appsink可以接收的数据格式，但和`appsrc`必须要设置caps属性以便后续和plugin的链接不同，appsink的caps属性为可选项，因为appsink处理的数据单元为GstSample，可以通过`gst_sample_get_caps()`直接从GstSample中获取到其下的GstCaps。

## signals

- `eos`：流结束信号，由stream线程发出。
- `new-preroll`：preroll sample可用信号，只有当`emit-signals`属性为`true`时才会由stream线程发出。
  - `preroll`：一个sink元素当且仅当有一个buffer进入pad之后才能完成`PAUSED`状态的转变，这个过程叫做`preroll`。为了能够尽快完成向`PLAYING`状态的转变，避免给用户造成视觉上的延迟，向pipeline中填充buffer(Preroll)是有很有必要的。Preroll在音视频同步方面是非常关键的，确保不会有buffer被sink元素抛弃。
- `new-sample`：新的sample可用信号，只有当`emit-signals`属性为`true`时才会由stream线程发出。

## GST_APP_API

### GstAppSinkCallbacks

```c
typedef struct {
  void          (*eos)              (GstAppSink *appsink, gpointer user_data);
  GstFlowReturn (*new_preroll)      (GstAppSink *appsink, gpointer user_data);
  GstFlowReturn (*new_sample)       (GstAppSink *appsink, gpointer user_data);

  /*< private >*/
  gpointer     _gst_reserved[GST_PADDING];
} GstAppSinkCallbacks;
```

- `*eos`：`eos`信号触发的回调函数指针
- `*new_preroll`：`new_preroll`信号触发的回调函数指针
- `*new_sample`：`new_sample`信号触发的回调函数指针
- `*user_data`：用户向回调函数传递的数据。

### pull-sample

通常开发者可以使用`gst_app_sink_pull_sample()`和`gst_app_sink_pull_preroll()`来获取appsink中的GstSample， 这两个方法将block线程直到appsink中获取到可用的GstSample或者Pipeline停止播放(end-of-stream)，同时还提供了timeout版本：`gst_app_sink_try_pull_sample`和`gst_app_sink_try_pull_preroll`。

- `gst_app_sink_pull_sample`

  ```c
  GstSample *
  gst_app_sink_pull_sample (GstAppSink * appsink)
  ```

- `gst_app_sink_try_pull_sample`

  ```c
  GstSample *
  gst_app_sink_try_pull_sample (GstAppSink * appsink,
                                GstClockTime timeout)
  ```

​	**注：**appsink内部使用一个队列来保存来自stream线程输出的buffer，假如应用程序pull-sample的速度不够快，那么队列将占用越来越多的内存，通常建议使用`max-buffers`属性设置内部队列长度，同时配合`drop`属性用于设置内部队列在队满时是丢帧或者block来避免内存泄露。

## Action signals

- `pull-sample`：

  ```c
  g_signal_emit_by_name (self, "pull-sample", user_data, &ret);
  ```

  - 将阻塞线程，直到获取到一个可用的GstSample，或收到EOS信号或者appsink插件状态变为`READY`或`NULL`。
  - 只有在appsink处于`PLAYING`状态下才会返回GstSample到user_data，所有新到达的GstSample都会加入appsink的内部队列，因此应用程序可以根据自己的需求以一定的速度来pull sample，但加入消耗速度不够快将造成大量的内存开销。

- `pull-preroll`

  ```c
  g_signal_emit_by_name (self, "pull-preroll", user_data, &ret);
  ```

  - 获取Appsink的最后一个preroll sample，即使得appsink变为`PAUSED`状态的sample。

**注：**假设`pull-sample`或`pull-preroll`操作返回的GstSample为空，那么appsink处于停止或者EOS状态，可以使用`gst_app_sink_is_eos()`进行查看。

## 代码实例

```c++
// 使用GST_APP_API和Action signal的方式
void CreatePipeline()
{
    // ...

	if (!(m_appsink = gst_element_factory_make ("appsink", "appsink"))) {
        LOG_ERROR_MSG ("Failed to create element appsink named appsink");
        goto exit;
    }

    // equals to gst_app_sink_set_emit_signals (GST_APP_SINK_CAST (m_appsink), true);
    g_object_set (m_appsink, "emit-signals", TRUE, NULL);

    // full definition of appsink callbacks
    /*
    GstAppSinkCallbacks callbacks = {cb_appsink_eos,
                            cb_appsink_new_preroll, cb_appsink_new_sample};
    gst_app_sink_set_callbacks (GST_APP_SINK_CAST (m_appsink),
        &callbacks, reinterpret_cast<void*> (this), NULL);
    */
    g_signal_connect (m_appsink, "new-sample",
        G_CALLBACK (cb_appsink_new_sample), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_appsink, NULL);
    
    //...
}

GstFlowReturn cb_appsink_new_sample (
    GstElement* appsink,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_appsink_new_sample called, user data: %p", user_data);

    SinkPipeline* sp = reinterpret_cast<SinkPipeline*> (user_data);
    GstSample* sample = NULL;
    GstBuffer* buffer = NULL;
    GstMapInfo map;
    const GstStructure* info = NULL;
    GstCaps* caps = NULL;
    GstFlowReturn ret = GST_FLOW_OK;
    int sample_width = 0;
    int sample_height = 0;

    // equals to gst_app_sink_pull_sample (GST_APP_SINK_CAST (appsink), sample);
    g_signal_emit_by_name (appsink, "pull-sample", &sample, &ret);
    if (ret != GST_FLOW_OK) {
        LOG_ERROR_MSG ("can't pull GstSample.");
        return ret;
    }

    if (sample) {
        buffer = gst_sample_get_buffer (sample);
        if ( buffer == NULL ) {
            LOG_ERROR_MSG ("get buffer is null");
            goto exit;
        }

        gst_buffer_map (buffer, &map, GST_MAP_READ);

        caps = gst_sample_get_caps (sample);
        if ( caps == NULL ) {
            LOG_ERROR_MSG ("get caps is null");
            goto exit;
        }

        info = gst_caps_get_structure (caps, 0);
        if ( info == NULL ) {
            LOG_ERROR_MSG ("get info is null");
            goto exit;
        }

        // -------- Read frame and convert to opencv format --------
        // convert gstreamer data to OpenCV Mat, you could actually
        // resolve height / width from caps...
        gst_structure_get_int (info, "width", &sample_width);
        gst_structure_get_int (info, "height", &sample_height);

        // customized user action
        {
            // init a cv::Mat with gst buffer address: deep copy
            // sometime you may got a empty buffer
            if (map.data == NULL) {
                LOG_ERROR_MSG("appsink buffer data empty\n");
                return GST_FLOW_OK;
            }

            cv::Mat img (sample_height, sample_width, CV_8UC3,
                        	(unsigned char*)map.data, cv::Mat::AUTO_STEP);
            img = img.clone();

            // redirection outside operation: for decoupling use
            if (sp->m_putDataFunc) {
                sp->m_putDataFunc(std::make_shared<cv::Mat> (img),
                    sp->m_putDataArgs);
            } else {
                goto exit;
            }
        }
    }

exit:
    if (buffer) {
        gst_buffer_unmap (buffer, &map);
    }
    if (sample) {
        gst_sample_unref (sample);
    }
    return GST_FLOW_OK;
}
```

### customized user action

```c++
{
    cv::Mat img (sample_height, sample_width, CV_8UC3,
                	(unsigned char*)map.data, cv::Mat::AUTO_STEP);
    // deep copy
    img = img.clone();
}
```

在示例代码中有以上这段，为了在每一帧图像上画框，我使用了OpenCV的接口，因此需要将GstBuffer中的数据转化为`cv::Mat`。`GstBuffer`的数据存放的真实地址由相关的`GstMapInfo`管理，我使用映射的地址`map.data`来构造了一个`cv::Mat`对象。这次构造是浅拷贝，但在这之后我使用`cv::Mat.clone()`方法做了一次深拷贝，这次深拷贝的原因是在映射`gst_buffer_map (buffer, &map, GST_MAP_READ);`时我只申请了`READ`权限。

为什么不申请`WRITE`权限呢，是因为在实际使用过程中发现一旦我同时申请`WRITE`权限，程序终端将会输出一个报错：尝试向一个不可写的Buffer申请写权限，并且我拿到的`GstBuffer`是一个空的buffer，其下的数据也为空。

```c
gboolean
gst_buffer_map (GstBuffer * buffer,
                GstMapInfo * info,
                GstMapFlags flags)
```

查看`gst_buffer_map ()`的API说明可以知道，当你请求映射的buffer是可写的但memory是不可写的时候，将自动生成并返回一个可写的拷贝，同时用这个拷贝替换掉只读的buffer。

buffer可写但memory不可写的和硬解码相关，硬解码需要相应的硬件配合，对这类memory的读写通常需要通过相关的驱动接口，在示例代码的pipeline中我使用了高通平台下的硬件解码器`qtivdec`，底层依赖于ION，关于ION的相关资料可以参考文章[ION Memory Control](https://ricardolu.gitbook.io/trantor/ion-memory-control)，文章描述了ION Buffer的使用方法，简单来说就是需要我们拿到ION Buffer的句柄，然后通过`mmap`将这块ION Memory映射到用户空间，才能对其进行操作。但是这个ION Buffer是在`qtivdec`插件中申请的，因此假如想要拿到它的句柄需要修改`qtivdec`的源码，维护这个句柄的生存周期并在sink这个buffer的时候将其加入`GstBuffer`的`GstStruture`结构中。

**注：**因为我目前基于高通平台开发，为了性能我在插件的选择上会尽可能选择具备硬件加速的插件，这就为程序引入了不可控因素，但在一定程度上是值得的。虽然这是个教学文档，但同时也是我的学习过程，因此我会将我在开发过程中遇到的一些问题和解决思路记录下来，供大家参考。

### 资源释放

样例代码由于进行了深拷贝并且将cv::Mat对象交给智能指针来管理，因此我们可以在回调完成之后手动释放相关的GstSample，释放GstSample的同时会自动释放其下的GstBuffer因此我们只用解除GstBuffer的映射。

在通常开发中完全可以appsink直接输出GstSample或者GstBuffer，并在不需要的时候再释放，以实现零拷贝。
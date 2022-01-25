# Build Pipeline

GStreamer提供了一个命令行工具`gst-launch-1.0`用于快速构建运行Pipeline，同样的GStreamer也提供了C-API用于在C/C++开发中引入GStreamer Pipeline，以下是构建GStreamer Pipeline的两种方式。

- **Github：[build-pipeline](https://github.com/gesanqiu/gstreamer-example/tree/main/application_develop/build_pipeline)**

## gst_parse_launch()

`gst_parse_launch()`是[GstParse](https://gstreamer.freedesktop.org/documentation/gstreamer/gstparse.html?gi-language=c#gstparse-page)的一个函数，`GstParse`允许开发者基于`gst-launch-1.0`命令行形式创建一个新的pipeline。

注：相关函数采取了一些措施来创建动态管道。因此这样的管道并不总是可重用的（例如，将状态设置为NULL并返回到播放）。

```c
GstElement *
gst_parse_launch (const gchar * pipeline_description,
                  GError ** error)
```

基于`gst-launch-1.0`命令行形式创建一个新的pipeline。

- `pipeline_description`：描述pipeline的命令行字符串
- `error`：错误提示信息

### GstParseError

- `GST_PARSE_ERROR_SYNTAX (0)`：Pipeline格式错误
- `GST_PARSE_ERROR_NO_SUCH_ELEMENT (1)`：Pipeline包含未知GstElement(Plugin)
- `GST_PARSE_ERROR_NO_SUCH_PROPERTY (2)`：Pipeline中某个GstElment(Plugin)设置了不存在属性
- `GST_PARSE_ERROR_LINK (3)`：Pipeline中某对Plugin之间的GstPad无法连接
- `GST_PARSE_ERROR_COULD_NOT_SET_PROPERTY (4)`：Pipeline中某个GstElment(Plugin)的属性设置错误
- `GST_PARSE_ERROR_EMPTY_BIN (5)`：Pipeline中引入了空的GstBin
- `GST_PARSE_ERROR_EMPTY (6)`：Pipeline为空
- `GST_PARSE_ERROR_DELAYED_LINK (7)`：Pipeline中某个GstPad存在阻塞

### Code Example

```c++
#include <gst/gst.h>
#include <string>
#include <iostream>

int main(int argc, char* argv[])
{
    GstElement* pipeline;
	GError* error = NULL;
    
    std::string m_strPipeline("filesrc location=test.mp4
                              	! qtdemux ! qtivdec ! waylandsink");

    pipeline = gst_parse_launch (m_pipeline.c_str(), &error);
    if ( error != NULL ) {
        printf ("Could not construct pipeline: %s", error->message);
        g_clear_error (&error);
    }

    // ...

    return 0;
}
```

使用`gst_parse_launch()`解析完之后就能够获得一条`GstPipeline`，然后就可以使用`gst_element_set_state()`来运行pipeline了。

## gst_element_factory_make()

`gst_element_factory_make()`是[GstElementFactory](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelementfactory.html?gi-language=c#gstelementfactory-page)的一个函数，`GstElementFactory`用于实例化一个`GstElement`。

开发人员可以使用 `gst_element_factory_find()` 和`gst_element_factory_create()`来实例化一个GstElement或者直接使用`gst_element_factory_make()`来实例化。

```c++
#include <gst/gst.h>

int main(int argc, char* argv[])
{
    GstElement* src;
    GstElementFactory* srcfactory;

    gst_init (&argc, &argv);

    {
        srcfactory = gst_element_factory_find ("filesrc");
        g_return_if_fail (srcfactory != NULL);
        src = gst_element_factory_create (srcfactory, "src");
        g_return_if_fail (src != NULL);
    }/*equals*/{
        src = gst_element_factory_make ("filesrc", "src");
    }
    
    // ...

    return 0;
}
```

`gst_element_factory_make()`只是创建并实例化单个的`GstElement`，如果要构建`GstPipeline`，那么还需要将一系列的`GstElement`添加到`GstPipeline`中，并按照正确的顺序链接。

### gst_bin_add_many()

`gst_bin_add_many()`函数将`GstElement`添加到pipeline中（不区分先后顺序）

`gst_bin_add_many()`只是将各个`GstElement`加入一个`GstBin`中，即`GstElement`的`parent`指针指向同一个`GstBin/GstPipeline`，这种添加是无序的，开发人员需要使用`gst_element_link_many()`来将这些`GstElement`链接起来。

### gst_element_link_many()

```c
gboolean
gst_element_link_many (GstElement * element_1,
                       GstElement * element_2,
                       ... ...)
```

- 参数为按顺序排列的`GstElement`变量，并且需要以`NULL`结尾。
- 在链接之前需要调用`gst_bin_add_many()`，确保所有的`GstElement`属于同一个`GstBin/GstPipeline`。

### gst_element_link_pads()

```c
// gst_element_link_many()的调用流程
gst_element_link_many()
    ->gst_element_link()
    	->gst_element_link_pads()
    		->gst_element_link_pads_full()
```

观察调用流程可以看到两个`GstElement`的链接实际是两个`GstPad`的链接，即`src pad`与`sink pad`的链接。使用`gst-inspect-1.0`查看大部分GStreamer的插件的`sink pad`和`src pad`的`Availability`属性都是always，这意味着插件之间总是可以链接，但是也存在一些特例，比如说`qtdemux`：

```shell
Pad Templates:
  SINK template: 'sink'
    Availability: Always
    Capabilities:
      video/quicktime
      video/mj2
      audio/x-m4a
      application/x-3gp

  SRC template: 'video_%u'
    Availability: Sometimes
    Capabilities:
      ANY

  SRC template: 'audio_%u'
    Availability: Sometimes
    Capabilities:
      ANY

  SRC template: 'subtitle_%u'
    Availability: Sometimes
    Capabilities:
      ANY

```

可以看到`qtdemux`具有三种`src pad`，在link阶段数据并未流通，这时候`qtdemux`的下一个插件无法知道`qtdemux`将会使用哪一个`src pad`，因此在链接`qtdemux`和其他插件时需要根据实际情况来分析。

在`GstElement`从`READY`状态切换到`PAUSED`状态时，上游`GstElement`的数据将会预流到`qtdemux`，在这个时候，`qtdemux`将会解析数据，然后配置stream信息，根据数据创建相应的`src pad`，完成这个操作之后，将会通过`gst_element_add_pad()`将`GstPad`添加到`qtdemux`，在gst_element_add_pad()中，有以下这样的一行代码：

```
  /* emit the PAD_ADDED signal */
  g_signal_emit (element, gst_element_signals[PAD_ADDED], 0, pad);
```

这意味着当qtdemux创建完src pad的时候，将会发出一个信号，于是我们可以给qtdemux添加一个回调，接收这个`pad-added`信号，再调用gst_element_link_many()完成链接：

```c++
static void cb_qtdemux_pad_added (
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

    if (g_str_has_prefix (new_pad_type, "video/x-h264")) {
        LOG_INFO_MSG ("Linking video/x-raw");
        /* Attempt the link */
        v_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_queue0), "sink");
        ret = gst_pad_link (new_pad, v_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link video source with waylandsink");
            goto exit;
        }
    } else if (g_str_has_prefix (new_pad_type, "audio/mpeg")) {
        LOG_INFO_MSG ("Linking audio/x-raw");
        a_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_queue1), "sink");
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

// pipeline create
{
    g_signal_connect (m_qtdemux, "pad-added",
            G_CALLBACK(qtdemux_pad_added_cb), reinterpret_cast<void*> (this));
}
```

当文件源既有音频数据又有视频数据的时候，`pad-added`信号会触发两次`qtdemux_pad_added_cb`回调，为了完成正确的链接，`qtdemux`会根据解析出的数据格式创建不同的`src-pad`，`src-pad`下包含一个描述描述数据格式的`GstCaps`，我们可以获取`GstCaps`的`GstStructure`来获取到数据格式内容。

但这存在一个问题就是我们需要提前知道数据源的数据格式才能够选择正确的插件，视频目前通常的编码格式是`h264//h265`，音频有很多，假如是文件数据源可以使用`ffmpeg`工具来读取这部分信息：

```shell
ts@ts-OptiPlex-7070:~/Downloads$ ffmpeg -i 1-2.mp4 
ffmpeg version 4.3.1 Copyright (c) 2000-2020 the FFmpeg developers
  built with gcc 5.4.0 (Ubuntu 5.4.0-6ubuntu1~16.04.12) 20160609
  configuration: --enable-nonfree --enable-pic --enable-shared --prefix=/usr/local/ffmpeg
  libavutil      56. 51.100 / 56. 51.100
  libavcodec     58. 91.100 / 58. 91.100
  libavformat    58. 45.100 / 58. 45.100
  libavdevice    58. 10.100 / 58. 10.100
  libavfilter     7. 85.100 /  7. 85.100
  libswscale      5.  7.100 /  5.  7.100
  libswresample   3.  7.100 /  3.  7.100
[mov,mp4,m4a,3gp,3g2,mj2 @ 0x174a280] st: 0 edit list: 2 Missing key frame while searching for timestamp: 0
[mov,mp4,m4a,3gp,3g2,mj2 @ 0x174a280] st: 0 edit list 2 Cannot find an index entry before timestamp: 0.
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '1-2.mp4':
  Metadata:
    major_brand     : mp42
    minor_version   : 0
    compatible_brands: isommp42
    creation_time   : 2021-01-29T02:37:38.000000Z
    location        : +40.0004+116.3568/
    location-eng    : +40.0004+116.3568/
    com.android.version: 11
    com.android.manufacturer: Xiaomi
    com.android.model: M2011K2C
  Duration: 00:03:39.03, start: 0.000000, bitrate: 12391 kb/s
    Stream #0:0(eng): Video: h264 (High) (avc1 / 0x31637661), yuv420p(tv, bt709), 1920x1080, 12233 kb/s, SAR 1:1 DAR 16:9, 29.99 fps, 30 tbr, 90k tbn, 60 tbc (default)
    Metadata:
      creation_time   : 2021-01-29T02:37:38.000000Z
      handler_name    : VideoHandle
    Stream #0:1(eng): Audio: aac (LC) (mp4a / 0x6134706D), 48000 Hz, stereo, fltp, 96 kb/s (default)
    Metadata:
      creation_time   : 2021-01-29T02:37:38.000000Z
      handler_name    : SoundHandle
At least one output file must be specified
```

可以看到我用的测试文件视频编码格式为`h264`，音频编码格式为`aac`，因此`qtdemux`解复用出来的音频应该接`h264parse`，音频应该接`aacparse`两个插件。然后通过`gst-inspect-1.0`查看这两个插件的`sink pad`能够接收的数据类型分别为`video/x-h264`和`audio/mpeg`，顺利完成caps的筛选和链接。

注：`gst_parse_launch()`因为使用了延迟链接，所以没有这个限制。

## 对比

在开发过程中，两种构建手段各有优劣：

- `gst_parse_launch`方便快捷，通常只要pipeline能通过`gst-launch-1.0`命令行工具成功运行起来也就能成功将pipeline构建起来，相对`gst_element_factory_make`来说代码量极少，在快速开发过程中具有不可比拟的优势，但是构建出来的pipeline过于依赖于Common Line String，动态调整能力不如`gst_element_factory_make`，并且由于内部实现对开发人员来说并不透明，因此也不便于应用开发。
- `gst_element_factory_make`开发人员自己精确维护每个插件(GstElement)的生存，相比`gst_parse_launch`拥有更加精细的控制粒度，这就要求开发人员对pipeline将要处理的数据和会用到的相关插件具有比较深刻的理解，上手难度较高。


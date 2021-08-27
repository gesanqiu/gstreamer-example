# Build Pipeline

GStreamer提供了一个命令行工具`gst-launch-1.0`用于快速构建运行Pipeline，同样的GStreamer也提供了C-API用于在C/C++开发中引入GStreamer Pipeline，以下是构建GStreamer Pipeline的两种方式。

- Github：[build-pipeline](https://github.com/gesanqiu/gstreamer-example/tree/main/application_develop/build_pipeline)

## gst_parse_launch()

`gst_parse_launch()`是[GstParse](https://gstreamer.freedesktop.org/documentation/gstreamer/gstparse.html?gi-language=c#gstparse-page)的一个函数，GstParse允许开发者基于`gst-launch-1.0`命令行形式创建一个新的pipeline。

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
    
    std::string m_strPipeline("gst-launch-1.0 filesrc location=test.mp4
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

## gst_element_factory_make()

`gst_element_factory_make()`是[GstElementFactory](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelementfactory.html?gi-language=c#gstelementfactory-page)的一个函数，GstElementFactory用于实例化一个GstElement。

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

### GstElement

[GstElement](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html?gi-language=c#gstelement-page)


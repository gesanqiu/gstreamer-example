# Interfaces

在应用程序中，将element定义为GObject对象便可沿用GObject中设置对象属性的方式，这为应用程序与element的交互带来便利。当然，此种设置对象属性的方式仅包含面向对象编程中常见的gette与setter，无法支持更复杂的交互需求。对于复杂交互需求，GStreamer使用一套基于GObject GTypeInterface类型的接口。
下文意图以简介形式带领读者了解各类接口，因此不包含源码。请读者在有需要了解更多细节时参考API指南。

## The URI Handler interface

在我们目前展示的示例中，源节点仅出现过读取本地文件的‘filesrc’，但实际上GStreamer支持多种类型的数据源。
GStreamer不要求应用程序掌握任何关于URI的使用细节，例如对于某种特定协议的网络源需使用哪类element。这些细节都已通过GstURIHandler接口进行抽象。
对于URI的命名没有什么严格的规则，一般而言，使用常见的命名规则即可，例如以下这些。
```c++
file:///<path>/<file>
http://<host>/<path>/<file>
rtsp://<host>/<path>
dvb://<CHANNEL>
...
```
应用程序可使用gst_element_make_from_uri()获取支持特定URI的源或接收器element，并根据需要将GST_URI_SRC或GST_URI_SINK用作GstURIType。
另外，可以使用Glib的g_filename_to_uri()和g_uri_to_filename()在文件名与URI之间进行转换。

## The Color Balance interface

GstColorBalance接口用于控制element中与视频相关的属性，如亮度、对比度等。它之所以存在，是因为据它作者所述，没有办法使用GObject动态注册这些视频相关属性。
xvimagesink，glimagesink和Video4linux2等若干element已实现GstColorBalance接口。

## The Video Overlay interface

GstVideoOverlay接口用于在应用程序窗口中嵌入视频流。应用程序可为实现该接口的element提供一个窗口句柄，然后该element利用此窗口句柄进行绘制，而不是另建一个新的顶层窗口。这种方式很利于在视频播放器中嵌入视频。
Video4linux2，glimagesink，ximagesink，xvimagesink和sdlvideosink等若干element已实现GstVideoOverlay接口。

## Other interfaces

GStreamer还提供了相当多的其他接口，并已由一些element实现。这些接口如下。
- GstChildProxy 访问multi-child element内部元素的属性
- GstNavigation 发送和解析navigation事件
- GstPreset 处理element预设
- GstRTSPExtension RTSP扩展接口
- GstStreamVolume 访问和控制流音量大小
- GstTagSetter 处理媒体元数据
- GstTagXmpWriter 进行XMP序列化
- GstTocSetter 设置和检索类TOC数据
- GstTuner 射频调谐操作
- GstVideoDirection 视频旋转和翻转
- GstVideoOrientation 控制视频方向
- GstWaylandVideo Wayland视频接口

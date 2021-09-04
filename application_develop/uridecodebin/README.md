<!--
 * @Description: README of uridecodebin
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:09:29
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-09-03 23:55:45
-->
# uridecodebin

uridecodebin是属于Playback部分的内容，内部集成了一系列自动化操作，可以有效缩短pipeline的元素，但是整个pipeline的构建过程对用户并不透明，因此不能很好的控制内部元素的链接，这需要用户做一定的取舍。

参考文档：
- [uridecodebin](https://gstreamer.freedesktop.org/documentation/playback/uridecodebin.html?gi-language=c#uridecodebin-page)
- [decodebin](https://gstreamer.freedesktop.org/documentation/playback/decodebin.html?gi-language=c#decodebin-page)
- [Playback tutorial 3: Short-cutting the pipeline](https://gstreamer.freedesktop.org/documentation/tutorials/playback/short-cutting-the-pipeline.html#)
- [Basic tutorial 3: Dynamic pipelines](https://thiblahute.github.io/GStreamer-doc/tutorials/basic/dynamic-pipelines.html?gi-language=c)

## build & run
```shell
cmake -H. -Bbuild/
cd build
make

# filesrc
./uridecoderbin --srcuri file:///user/local/gstreamer-example/application_develop/video.mp4

# rtspsrc
./uridecoderbin --srcuri rtsp://admin:1234@10.0.23.227:554

# souphttpsrc
./uridecoderbin --srcuri https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm
```
**注：**`uridecodebin`的`uri`属性必须是绝对路径。
# Build Pipeline

GStreamer提供了一个命令行工具`gst-launch-1.0`用于快速构建运行Pipeline，同样的GStreamer也提供了C-API用于在C/C++开发中引入GStreamer Pipeline，本仓库是构建GStreamer Pipeline的两种方式的代码实例。

**教程地址：[Build Pipeline](https://ricardolu.gitbook.io/gstreamer/application-development/build-pipeline)**

相关文档：

- [GstParse](https://gstreamer.freedesktop.org/documentation/gstreamer/gstparse.html?gi-language=c#gstparse-page)
- [GstElement](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelement.html?gi-language=c#gst_element_link)
- [GstElementFactory](https://gstreamer.freedesktop.org/documentation/gstreamer/gstelementfactory.html?gi-language=c#gstelementfactory-page)

## Build

```cmake
# CMakeLists.txt Option
# Build gst_parse_launch.cpp
## OPTION(COMPILE_PARSE_LAUNCH "build gst_parse_launch" OFF)
cmake -H. -Bbuild -DCOMPILE_PARSE_LAUNCH=ON -DCOMPILE_FACTORY_MAKE=OFF

# Build gst_element_factory_make.cpp
## OPTION(COMPILE_FACTORY_MAKE "build gst_element_factory_make" OFF)
cmake -H. -Bbuild -DCOMPILE_PARSE_LAUNCH=OFF -DCOMPILE_FACTORY_MAKE=ON

cd build
make
```

### Run

```shell
# gst_parse_launch
./GstParse --srcuri ../video.mp4

# gst_element_factory_make
./GstElementFactory --srcuri ../video.mp4
```

上述程序运行结果等同于如下Pipeline：

```shell
gst-launch-1.0 filesrc location=test.mp4 ! qtdemux ! qtivdec ! waylandsink
```


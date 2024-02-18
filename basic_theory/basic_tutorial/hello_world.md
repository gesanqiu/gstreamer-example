# Basic tutorial1: Hello world!

## 目标

大部分人对大多数编程教程的第一印象都是在屏幕上输出”Hello World“，但是GStreamer作为一个多媒体框架，将以播放一个视频作为第一个教程。

## Hello world

### basic-tutorial-1.c

```c
#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Build the pipeline */
  pipeline =
      gst_parse_launch
      ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm",
      NULL);

  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
```

上述代码将打开一个窗口并显示一个带有音频的电影。由于这段媒体是从网络上获取的，所以可能需要等待几秒才会显示窗口，等待时间取决于网络连接的速度。此外，没有延迟管理(缓冲)，所以当网速比较慢的时候，电影可能会在几秒钟后停止。具体可以参考[Basic tutorial 12: Streaming](https://gstreamer.freedesktop.org/documentation/tutorials/basic/streaming.html)以获得解决方案。

### 编译

GStreamer目前支持大部分主流的开发平台，包括x86/arm64架构的Linux/Mac OS X/Windows，但是考虑到编译运行的便利性，更建议以Linux作为学习平台，官方的[安装文档](https://gstreamer.freedesktop.org/documentation/installing/on-linux.html?gi-language=c)展示了如何在各个Linux发行版本安装GStreamer，并且在使用Gcc编译时添加以下选项以链接GStreamer库文件：

```shell
pkg-config --cflags --libs gstreamer-1.0

# 完整的编译指令
gcc basic-tutorial-1.c -o basic-tutorial-1 `pkg-config --cflags --libs gstreamer-1.0`
# 运行
./basic-tutorial-1
```

Linux平台下的大部分开发软件包使用PackageConfig进行的管理，使用命令行的方式无法适应大型项目的需求，通常情况下我会使用CMake进行项目的构建，在CMake中加入以下指令即可完成GStreamer相关库的链接工作：

```cmake
cmake_minimum_required(VERSION 3.10)

project(basic-tutorial-1)
set(CMAKE_CXX_STANDARD 11)

include(FindPkgConfig)	# equals `pkg-config --cflags --libs gstreamer-1.0`
pkg_check_modules(GST    REQUIRED gstreamer-1.0)
pkg_check_modules(GSTAPP REQUIRED gstreamer-app-1.0)

include_directories(
    ${GST_INCLUDE_DIRS}
    ${GSTAPP_INCLUDE_DIRS}
)

link_directories(
    ${GST_LIBRARY_DIRS}
    ${GSTAPP_LIBRARY_DIRS}
)

add_executable(${PROJECT_NAME}
    basic-tutorial-1.c
)

target_link_libraries(${PROJECT_NAME}
    ${GST_LIBRARIES}
    ${GSTAPP_LIBRARIES}
)
```

在这里就不介绍CMake的过多细节，关于CMake的链接你可以浏览[CMake 查找已有库](https://app.gitbook.com/@ricardolu/s/trantor/cmake-in-action/cmake-tutorial/cha-zhao-yi-you-ku)获得更多相关信息。

```shell
cmake -H. -Bbuild/
cd build
make
./basic-tutorial-1
```

## 工作流

### 初始化

```c
  /* Initialize GStreamer */
  gst_init (&argc, &argv);
```

GStreamer程序的第一行代码必须是`gst_init (&argc, &argv)`，它完成了以下工作：

- 初始化所有内部结构体
- 检查平台所有可用的插件
- 执行任何用于GStreamer的命令行选项

假设在程序中没有这行代码，编译能够通过，但在运行时将抛出异常，不一定是找不到GStreamer的结构体，就我的经验而言会是无法加载你所用的插件。传递的参数可以是两个`NULL`，但更建议传递命令行参数。

### 构建Pipeline

```c
  pipeline =
      gst_parse_launch
      ("playbin uri=https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm",
      NULL);
```

这行代码是这篇教程的核心，它构建了一个只有`playbin`插件的pipeline。

GStreamer是一个为处理多媒体流而设计的框架，媒体从source元素(生产者)，流动到sink元素(消费者)，经过一系列执行各种任务的中间element。这些element集合被称为“pipeline”。

通常你可以通过手动组装各个独立的element来构建pipeline，但假如pipeline足够简单，你可以使用GStreamer的高级特性：`gst_parse_launch()`。

这个函数将解析一条文本形式表示的pipeline并将其转化成一条真实的pipeline，使用起来十分便利。

GStreamer提供了两种构建pipeline的方式，可以浏览[Build Pipeline](https://ricardolu.gitbook.io/gstreamer/application-development/build-pipeline)以获得更多信息。

### Playbin

[playbin]()是一个特殊的element，它具备一个pipeline的所有特点，既有source，又有sink。它在内部自动创建和连接所有播放媒体需要的element，用户不需要在意这些细节。

它不具备手动管道所具有的控制粒度，但是它仍然允许足够的定制以满足广泛的应用程序。包括本教程，传递了一个http源，用户可以尝试传递file源或是rtsp源，playbin将为不同的源显示地初始化合适的source element。

```c
  /* Start playing */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
```

当一切准备就绪，需要将pipeline的状态设置为`PLAYING`以开始播放。

### 监听Pipeline

```c
  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
```

上述代码监听整个pipeline总线，`gst_element_get_bus ()`检索pipeline的bus，`gst_bus_timed_pop_filtered()`阻塞程序直到程序运行引发ERROR或者到达EOS。

至此，GStreamer将接管整个程序，假如你的URI有错误或者文件不存在，或者缺少plug-in，GStreamer提供了几种通知机制，但在这个教程中一旦发生错误程序就将退出运行。

### 资源回收

```c
  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
```

在程序终止之前，需要回收所有手动申请的资源，建议用户总是阅读所用的函数文档以确认是否需要释放所使用的各类资源。

`gst_bus_timed_pop_filtered()`返回了一个需要使用`gst_message_unref()`释放的GstMessage，`get_element_get_bus()`将为pipeline的bus添加引用计数，需要使用`get_object_unref()`释放。在释放pipeline及其下的所有element之前需要将pipeline的状态设置为`NULL`.

## 总结

本教程展示了一下内容：

- 如何使用CMake构建GStreamer程序。
- 如何使用`gst_init()`初始化GStreamer。
- 如何使用`gst_parse_launch()`快速构建一条pipeline。
- 如何使用playbin创建一条自动的播放pipeline。
- 如何使用`gst_element_set_state()`向GStreamer发送开始播放信号。
- 如何使用`gst_element_get_bus()`和`get_bus_timed_pop_filtered()`监听pipeline并进行相关处理。



原文地址：https://gstreamer.freedesktop.org/documentation/tutorials/basic/hello-world.html?gi-language=c#walkthrough

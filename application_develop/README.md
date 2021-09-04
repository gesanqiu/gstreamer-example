# Application Development

## 概述

GStreamer作为一个音视频应用开发框架，提供了一个快速开发工具`gst-launch-1.0`，开发人员能够将现有的Pulgins以一定规则任意组合成一条Pipeline并运行起来。但这显然不能满足更高级的开发需求，对于开发人员来说我们往往需要对音视频的源数据进行操作，操作单位至少是一帧图片或一段音频，事实上这些数据就在Pipeline中以Stream的形式在各个Plugin之间传递，而为了能够操作这些数据，我们需要更高的访问权限，更细的控制粒度。

本章节旨在展示一个基于GStreamer框架的简单应用是如何被开发出来的，以及我们能够实现的功能。

本章节代码仓库：[application-develop](https://github.com/gesanqiu/gstreamer-example/tree/main/application_develop)

章节内容：

- 构建pipeline的两种方式：gst_parse_launch()和gst_element_factory_make()
- uridecodebin
- appsink/appsrc
- GstBufferPool
- GstPadProbe
- 自定义plugin

## 开发平台
- 开发平台：Qualcomm® QRB5165 (Linux-Ubuntu 18.04)
- 图形界面：Weston(Wayland)
- 开发框架：GStreamer， OpenCV
- 第三方库：gflags，json-glib-1.0，glib-2.0
- 构建工具：[CMake](https://ricardolu.gitbook.io/trantor/cmake-in-action)

## 协议

本章节所有代码均遵守`GPL_v3`开源协议。
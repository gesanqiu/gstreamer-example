# GStreamer-example

[![img](images/Author-@RicardoLu-red.svg)](https://github.com/gesanqiu)![img](images/Version-1.0.0-blue.svg)[![img](images/gstreamer-example.svg)](https://github.com/gesanqiu/gstreamer-example)![img](images/license-GPLv3-000000.svg)

[GStreamer](https://gstreamer.freedesktop.org/documentation/index.html?gi-language=c)是一个非常强大和通用的用于开发流媒体应用程序的框架。GStreamer框架的许多优点都来自于它的模块化：GStreamer可以无缝地合并新的插件模块，但是由于模块化和强大的功能往往以更大的复杂度为代价，开发新的应用程序并不总是简单。

出于以下两点原因，让我萌生了发起这个项目的想法：

- 网络上关于GStreamer的开发文档比较少，几乎只能依靠官方的[API Reference](https://gstreamer.freedesktop.org/documentation/libs.html?gi-language=c)和[Tutorials](https://gstreamer.freedesktop.org/documentation/tutorials/index.html?gi-language=c)英文文档；
- 目前项目只有我一个人在维护，因此更多是出于我个人开发的学习记录，但欢迎各位的加入。

## 更新计划‌

### 基础理论

本章节将介绍GStreamer的基本理论和[Gstreamer Core Library](https://gstreamer.freedesktop.org/documentation/gstreamer/gi-index.html?gi-language=c)中最常用的部分数据结构及其相关API，并且完成所有[Tutorial](https://gstreamer.freedesktop.org/documentation/tutorials/index.html?gi-language=c)的翻译。

### 应用开发

本章节将结合我的开发经历，讲解使用GStreamer开发一个视频流应用会需要用到的基础技术。

- 构建pipeline的两种方式：`gst_parse_launch()`和`gst_element_factory_make()`(done)
- uridecodebin详解(done)
- appsink/appsrc(done)
- GstBufferPool
- GstPadProbe(done)
- 自定义plugin

### 平台定制plugins

本章节将介绍`Qualcomm`和`Nvidia`两个平台的一些定制插件，由于我现在更多在`Qualcomm`平台上进行开发，并且`Nvidia`有相对健全的Issue机制和论坛维护，**因此**`Nvidia`**仅作为补充内容，更新计划待定**。

- [Qualcomm GStreamer Plugins](https://developer.qualcomm.com/qualcomm-robotics-rb5-kit/software-reference-manual/application-semantics/gstreamer-plugins)
  - qtivdec
  - qtivtransform
  - qtioverlay(done)
- [Nvidia GStreamer Plugin Overview](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_Intro.html)
  - nvvideoconvert
  - nvdsosd
  - nvvideo4linux2

**注：**作者才疏学浅，如有纰漏，欢迎指正。

## 联系方式‌

- 在线阅读：https://ricardolu.gitbook.io/gstreamer/
- Github：https://github.com/gesanqiu/gstreamer-example
- E-mail：[shenglu1202@163.com](mailto:shenglu1202@163.com)
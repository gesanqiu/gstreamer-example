# GStreamer-example

[![](https://img.shields.io/badge/Auther-@RicardoLu-red.svg)](https://github.com/gesanqiu)![](https://img.shields.io/badge/Version-2.0.0-blue.svg)[![](https://img.shields.io/github/stars/gesanqiu/gstreamer-example.svg?style=social&label=Stars)](https://github.com/gesanqiu/gstreamer-example)

[GStreamer](https://gstreamer.freedesktop.org/documentation/index.html?gi-language=c)是一个非常强大和通用的用于开发流媒体应用程序的框架。GStreamer框架的许多优点都来自于它的模块化：GStreamer可以无缝地合并新的插件模块，但是由于模块化和强大的功能往往以更大的复杂度为代价，开发新的应用程序并不总是简单。

出于以下两点原因，让我萌生了发起这个项目的想法：

- 网络上关于GStreamer的开发文档比较少，几乎只能依靠官方的[API Reference](https://gstreamer.freedesktop.org/documentation/libs.html?gi-language=c)和[Tutorials](https://gstreamer.freedesktop.org/documentation/tutorials/index.html?gi-language=c)英文文档；
- 目前项目只有我一个人在维护，因此更多是出于我个人开发的学习记录，但欢迎各位的加入。

## 更新日志

- 2022.07.17：更新基于deepstream-6.1开发的pipeline，后续用于集成yolov5s.trt模型。
- 2022.02.10：增加更新日志，修改更新计划，整理已更新内容，删除多余的初始化文档，后续随缘更新。
- 2022.01.25：将Tutorial文档merge进来。
- 2021.10.31：更新`nvdsosd`插件教程。
- 2021.09,09：更新GstPadProbe教程。
- 2021.09.04：增加audio轨道处理分支。
- 2021.08.31：更新`uridecodebin`插件教程。
- 2021.08.29：更新`appsink/appsrc`插件教程。
- 2021.08.27：更新pipeline构建教程。
- 2021.08.26：更新`qtioverlay`插件教程。
- 2021.08.24：初始化提交。

## 更新计划‌

### 基础理论

本章节主要是[GStreamer Tutorial](https://gstreamer.freedesktop.org/documentation/tutorials/index.html?gi-language=c)的翻译。

### 应用开发

本章节将结合我的开发经历，讲解使用GStreamer开发一个视频流应用会需要用到的基础技术。

- 构建pipeline的两种方式：`gst_parse_launch()`和`gst_element_factory_make()`(done)
- uridecodebin详解(done)
- appsink/appsrc(done)
- GstPadProbe(done)
- 自定义plugin

### 平台定制plugins

本章节将介绍`Qualcomm`和`Nvidia`两个平台的一些定制插件，由于我现在更多在`Qualcomm`平台上进行开发，并且`Nvidia`有相对健全的Issue机制和论坛维护，**因此**`Nvidia`**仅作为补充内容，更新计划待定**。

- [Qualcomm GStreamer Plugins](https://developer.qualcomm.com/qualcomm-robotics-rb5-kit/software-reference-manual/application-semantics/gstreamer-plugins)
  - qtivdec
  - qtioverlay
- [Nvidia GStreamer Plugin Overview](https://docs.nvidia.com/metropolis/deepstream/dev-guide/text/DS_plugin_Intro.html)
  - nvdsosd

**注**：作者才疏学浅，如有纰漏，欢迎指正。

## 联系方式‌

- 在线阅读：https://ricardolu.gitbook.io/gstreamer/
- Github：https://github.com/gesanqiu/gstreamer-example
- E-mail：[shenglu1202@163.com](mailto:shenglu1202@163.com)

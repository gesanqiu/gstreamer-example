# app

为了完成应用程序与GStreamer Pipeline的数据交互，GStreamer提供了两个插件：

- [GstAppSink](https://gstreamer.freedesktop.org/documentation/applib/gstappsink.html) – 应用程序从管道中提取GstSample的简便方法
- [GstAppSrc](https://gstreamer.freedesktop.org/documentation/applib/gstappsrc.html) – 应用程序向管道中注入GstBuffer的简单方法

本仓库是这两个插件的应用实例，**教程地址：[GStreamer-app](https://ricardolu.gitbook.io/gstreamer/application-development/app)**

## Build & Run

```shell
cmake -H. -Bbuild/
cd build
make

./app --srcuri ../video.mp4
# you will see one green rectangle named appsink
# and one red rectangle named appsrc on your video
```


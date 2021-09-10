# GstPadProbe

GstPad上发生的数据流、事件和查询可以通过探针进行监控，探针通过`gst_pad_add_probe()`安装，这为开发者提供了另外一种访问GStreamer pipeline数据的方式。

**教程地址：[GstPadProbe](https://ricardolu.gitbook.io/gstreamer/application-development/gstpadprobe)**

参考文档：

- [GstPad](https://gstreamer.freedesktop.org/documentation/gstreamer/gstpad.html)
- [Basic tutorial 7: Multithreading and Pad Availability](https://gstreamer.freedesktop.org/documentation/tutorials/basic/multithreading-and-pad-availability.html?gi-language=c)
- [Buffers not writable after tee](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/609)

## build & run

```shell
mkdir build
cd build

cmake .. -DCOMPILE_RTSP_SOURCE=ON -DCOMPILE_FILE_SOURCE=OFF
make
# rtspsrc
./GstPadProbe --srcuri rtsp://admin:1234@10.0.23.227:554

cmake .. -DCOMPILE_RTSP_SOURCE=OFF -DCOMPILE_FILE_SOURCE=ON
make
# filesrc
./GstPadProbe --srcuri /user/local/gstreamer-example/application_develop/video.mp4
```


# Playback tutorial 8: Hardware-accelerated video decodingHardware-accelerated video decoding.

### Goal

随着低功耗设备变得越来越普遍，硬件加速视频解码已经迅速变为一种必需特性。这篇教程（实际上更像一篇讲义）阐述了硬件加速的一些背景以及GStreamer如何从中收益。

如果正确设置，你不需要做任何特殊工作来激活硬件加速；GStreamer将自动启用它。

### Introduction

对于CPU而言，视频解码是一个十分耗费资源的任务，尤其是针对1080p及以上的高分辨率视频。幸运的是，现代的图形卡都配备了可编程GPU，能够处理这部分工作，允许CPU关注其他任务。对于低功耗的CPU来说，拥有专用的硬件是必要的，因为这些CPU根本无法足够快地解码这些媒体。

在目前的情况下（2016年6月）各个GPU厂商都提供了不同的方法（API）来访问它们的硬件，而一个强大的行业标准还没有出现。

注：接下来的部分为各大芯片厂商的硬件编解码协议的简要介绍，这里不做翻译，因为对于有这部分需求的用户而言这些内容其实没有太大的价值。

### Inner workings of hardware-accelerated video decoding plugins

这些API通常提供许多功能，例如视频解码，预处理或解码帧的显示。同样的，这些插件（厂商维护的硬件相关的插件）通常也为这些功能提供了不同的GStreamer element，因此pipeline的构建不会受到这些因素影响。

例如，`gstreamer-vaapi`系列插件提供了`vaapidecode`，`vaapipostproc`和`vaapisink`元素，这几个元素能够通过VAAPI分别完成硬件加速解码，将裸的视频帧传递给GPU内存，将GPU帧传递给系统内存以及显示GPU帧。

区分传统的GStreamer帧(驻留在系统内存中)和硬件加速API生成的帧是很重要的。后者驻留在GPU内存中，并且GStreamer无法访问这部分内存。GPU帧通常可以映射到系统内存中，这时候可以将其视为传统的GStreamer帧，但将它们留在GPU中并从那里显示出来要高效得多。

GStreamer需要跟踪这些“硬件缓冲区”的位置，以确保传统的缓冲区仍然从一个element传递到另一个element。这些硬件缓冲区使用起来就像常规的缓冲区，但是映射它们的内容要慢得多，因为它必须从硬件加速元素使用的特殊内存中检索出来。

以上意味着，假如当前系统中支持特定的硬件加速API，并且相应的GStreamer插件也可用，类似于`playbin`这种拥有auto-plugging机制的elements可以使用硬件加速来构建pipeline；应用程序几乎不需要做任何特殊的事情来启用它。

当`playbin`必须在不同的但都可用的elements中进行选择时，例如传统的软件解码其（如`vp8dec`）和硬件加速解码器（`vaapidecode`），它通过这两个解码器element在GStreamer中注册的rank决定到底使用哪一个。rank是每个元素的一个属性，表示其优先级；`playbin`将选择符合构建完整pipeline需求且拥有最高优先级的element。

因此`playbin`是否使用硬件加速取决于所有可处理当前media type的elements的rank。于是最简单的使能或禁用硬件加速功能的方法就是改变与其相关的elements的rank，如下面代码：

```c++
static void enable_factory (const gchar *name, gboolean enable) {
    GstRegistry *registry = NULL;
    GstElementFactory *factory = NULL;

    registry = gst_registry_get_default ();
    if (!registry) return;

    factory = gst_element_factory_find (name);
    if (!factory) return;

    if (enable) {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_PRIMARY + 1);
    }
    else {
        gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE (factory), GST_RANK_NONE);
    }

    gst_registry_add_feature (registry, GST_PLUGIN_FEATURE (factory));
    return;
}
```

`enable_factory`函数的第一个参数是想要修改的element的name属性值，例如`vaapidecode`或`fluvadec`。

核心方法是`gst_plugin_feature_set_rank`，它将把请求的element factory的rank修改为期望值。为了方便起见，rank被分为NONE，MARGINAL，SECONDARY和PRIMARY四个等级，但任意数值均可。当启用一个元素时，我们将其rank设置为PRIMARY+1，因此它的rank高于其他通常具有PRIMARY rank的元素。将一个元素的rank设置为NONE将使auto-plugging机制永远不会选择它。

注：当硬件解码器有缺陷时，GStreamer开发人员经常将其排名低于软件解码器。这应该是一个警告。

## Conclusion

这篇教程展示了GStreamer内部如何管理硬件加速视频解码。特别地，

- 如果有合适的API和相应的GStreamer插件可用，应用程序不需要做任何特殊的事情来启用硬件加速。
- 硬件加速可以通过使用`gst_plugin_feature_set_rank()`改变解码元素的rank来影响auto-plugging元素对其的选择。
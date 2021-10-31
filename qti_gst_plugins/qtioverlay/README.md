# qtioverlay

## Overview

高通的[qtioverlay](https://developer.qualcomm.com/qualcomm-robotics-rb5-kit/software-reference-manual/application-semantics/gstreamer-plugins/qtioverlay)插件可以在YUV（NV12/NV21）图片帧上绘制RGB和YUV，它支持在YUV帧顶层绘制静态纹理图，回归边框，用户自定义文本和日期/时间，qtioverlay具备以下特性：

- YUV帧可以来自于摄像头视频流或视频文件的解码；
- 绘制信息可以是机器学习算法输出的Metadata或直接使用GST Propertie来设置的用户自定义文本；
- Machine Learning Metadata支持检测、分割、分类和拍照定位等算法输出；
  - 一个meatadata关联一个overlay item，支持同时绘制多个不同类型overlay item，metadata的个数没有上限，但数量过多会影响性能；
  - 检测算法的metadata数据结构被设计为bbox和label信息；
  - 分类算法的metada数据结构则仅有label和confidence信息(分类结果通常作为检测结果的一部分，即分类的数据结构为检测的数据结构结构体的一个成员)；
- 支持overlay图片的格式为RGBA；
- 日期/时间和用户自定义文本内容只能通过设置GST Properties的方式来实现，也即这部分信息将和GST Pipeline绑定，一旦pipeline初始化完毕就无法改变。

**qtioverlay插件源码地址：**[**qtioverlay**](https://github.com/gesanqiu/gstreamer-example/tree/main/qti_gst_plugins/qtioverlay)

**例程地址：**[**qtioverlay-example**](https://github.com/gesanqiu/gstreamer-example/tree/main/qti_gst_plugins/qtioverlay/qtioverlay-example)

## Properties

- `name`：qtioverlay的name。
- `qos`：时间服务质量。
- `overlay-text`：用户自定义文本字符串。
- `overlay-data`：overlay日期字符串。
- `bbox-color`：overlay ML Metadata回归边框的颜色，RGBA（32位无符号数），默认为0x0000CCFF。
- `date-color`：overlay日期颜色，RGBA（32位无符号数），默认为0xFF0000FF。
- `text-color`：用户自定义文本颜色，RGBA（32位无符号数），默认为0xFFFF00FF。
- `pose-color`：overlay ML Metadata PostNet Type的颜色，RGBA（32位无符号数），默认为0x33CC00FF。

以上都是`qtioverlay`的GST Properties，一旦设置就与pipeline绑定，无法动态修改，并不适合开发使用，因此在开发中更多使用的是ML Metadata来完成绘制信息的传递。

## Develop Guide

`qtioverlay`所依赖的库主要有两个`qtimlmeta`和`qmmf_overlay`，`libqtimlmeta.so`为ML Metadata的实现，`libqmmf_overlay.so`为绘制的实现。

### qtimlmeta

- GstMLDetactionMeta

```c
/**
 * GstMLBoundingBox:
 * @x: horizontal start position
 * @y: vertical start position
 * @width: active window width
 * @height: active window height
 *
 * Bounding box properties
 */
struct _GstMLBoundingBox {
  guint x;
  guint y;
  guint width;
  guint height;
};

/**
 * GstMLClassificationResult:
 * @name: name for given object
 * @confidence: confidence for given object
 *
 * Name and confidence handle
 */
struct _GstMLClassificationResult {
  gchar  *name;
  gfloat confidence;
};

/**
 * GstMLDetectionMeta:
 * @parent: parent #GstMeta
 * @box: bounding box coordinates
 * @box_info: list of GstMLClassificationResult which handle names and confidences
 *
 * Machine learning SSD models properties
 */
struct _GstMLDetectionMeta {
  GstMeta           parent;
  GstMLBoundingBox  bounding_box;
  GSList            *box_info;
};
```

- GstClassificationResult

```c
/**
 * GstMLClassificationMeta:
 * @parent: parent #GstMeta
 * @result:  name and confidence
 * @location: location in frame of location is CUSTOM then x/y are considered
 * @x: horizontal start position if location is CUSTOM
 * @y: vertical start position if location is CUSTOM
 *
 * Machine learning classification models properties
 */
struct _GstMLClassificationMeta {
  GstMeta                   parent;
  GstMLClassificationResult result;
}
```

- gst_buffer_add_detection_meta()

```c
/**
 * gst_buffer_add_detection_meta:
 * @buffer: the buffer new metadata belongs to
 *
 * Creates new bounding detection entry and returns pointer to new
 * entry. Metadata payload is not input parameter in order to avoid
 * unnecessary copy of data.
 *
 */
GST_EXPORT
GstMLDetectionMeta * gst_buffer_add_detection_meta (GstBuffer * buffer);
GstMLDetectionMeta *
gst_buffer_add_detection_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  GstMLDetectionMeta *meta =
      (GstMLDetectionMeta *) gst_buffer_add_meta (buffer,
          GST_ML_DETECTION_INFO, NULL);

  return meta;
}
```

- 资源回收

```c
static void
gst_ml_detection_free (GstMeta *meta, GstBuffer *buffer)
{
  GstMLDetectionMeta *bb_meta = (GstMLDetectionMeta *) meta;
  g_slist_free_full(bb_meta->box_info, free);
  GST_DEBUG ("free detection meta ts: %llu ", buffer->pts);
}
```

GstMeta将随着GstBuffer的释放而自动释放，因此这部分资源的释放不需要用户来手动操作。

### ML Metadata

```c
{
    GstMLDetectionMeta *meta = gst_buffer_add_detection_meta(buffer);
    if (!meta) {
        TS_ERR_MSG_V ("Failed to create metadata");
        return ;
    }
​
    GstMLClassificationResult *box_info = (GstMLClassificationResult*)malloc(
        sizeof(GstMLClassificationResult));
​
    uint32_t label_size = g_labels[results->at(i).label].size() + 1;
    box_info->name = (char *)malloc(label_size);
    snprintf(box_info->name, label_size, "%s", g_labels[results->at(i).label].c_str());
​
    box_info->confidence = results->at(i).confidence;
    meta->box_info = g_slist_append (meta->box_info, box_info);
​
    meta->bounding_box.x = results->at(i).rect[0];
    meta->bounding_box.y = results->at(i).rect[1];
    meta->bounding_box.width = results->at(i).rect[2];
    meta->bounding_box.height = results->at(i).rect[3];
}
```

### qtiverlay

```c
/**
 * gst_overlay_apply_ml_bbox_item:
 * @gst_overlay: context
 * @metadata: machine learning metadata entry
 * @item_id: pointer to overlay item instance id
 *
 * Converts GstMLDetectionMeta metadata to overlay configuration and applies it
 * as bounding box overlay.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_ml_bbox_item (GstOverlay * gst_overlay, gpointer metadata,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLDetectionMeta * meta = (GstMLDetectionMeta *) metadata;
  GstMLClassificationResult * result =
      (GstMLClassificationResult *) g_slist_nth_data (meta->box_info, 0);

  GstVideoRectangle bbox;
  bbox.x = meta->bounding_box.x;
  bbox.y = meta->bounding_box.y;
  bbox.w = meta->bounding_box.width;
  bbox.h = meta->bounding_box.height;
  if (gst_overlay->meta_color) gst_overlay->bbox_color = meta->bbox_color;

  return gst_overlay_apply_bbox_item (gst_overlay, &bbox, result->name,
      gst_overlay->bbox_color, item_id);
}
```

在`gst_overlay_apply_bbox_item`中最终使用`gstoverlay->overlay`的成员函数`CreateOverlayItem`和`EnableOverlayItem`进行绘图，这部分的实现在`qmmf_overlay`中的

### 增加meta-color属性

```c
  meta-color          : Bounding box overlay use meta data color
                        flags: readable, writable
                        Boolean. Default: false
```

在`_GstMLDetectionMeta`结构体中增加一个`guint`类型的32位无符号整型变量`bbox_color`用于表示`qtipverlay`颜色所需的RGBA值，在`gstqtioverlay.cc`也即`qtioverlay`的源码中增加一个`gboolean`类型的`meta-color`变量用于判断是使用`bbox-color`属性设置的固定边框颜色还是从ML Metadata中取颜色值动态改变边框颜色。

```c
// modify of gstoverlay.cc
static void
gst_overlay_class_init (GstOverlayClass * klass)
{​  
  g_object_class_install_property (gobject, PROP_OVERLAY_META_COLOR,
    g_param_spec_boolean ("meta-color", "Meta color", "Bounding box overlay use meta data color",
      DEFAULT_PROP_OVERLAY_META_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

// 在gst_overlay_init()中增加初始化
// 在gst_overlay_get_property()和gst_overlay_set_property()增加相关的操作属性的方法
g_value_set_boolean (value, gst_overlay->meta_color);
gst_overlay->meta_color = g_value_get_boolean (value);

// 在绘画函数gst_overlay_apply_ml_bbox_item()中增加bbox_color的赋值语句
if (gst_overlay->meta_color) gst_overlay->bbox_color = meta->bbox_color;

// 在应用生成ML Meta的时候将BBox RGB各通道值转换为一个表示RGBA四通道的32位无符号数
meta->bbox_color = (r << 24) + (g << 16) + (b << 8) + 0xFF;
```

### 增加meta-thick属性

在gstoverlay中gst_overlay_apply_ml_bbox_item调用gst_overlay_apply_bbox_item完成bbox item的绘制，：

```c
// gst_overlay_apply_ml_bbox_item
gst_overlay_apply_bbox_item (gst_overlay, &bbox, result->name,
      gst_overlay->bbox_color, item_id);
// gst_overlay_apply_bbox_item()
gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
gst_overlay->overlay->EnableOverlayItem (*item_id);

// gstoverlay.cc的实现依赖于libqmmf_overlay.so
// ==============>qmmf_overlay.cc<===============//
// EnableOverlayItem()
overlayItem->Activate(true);

/* ======================================================================> */
// 在gstoverlay.cc中初始化qtioverlay这个插件时将gst_overlay_transform_frame_ip
// 注册为VideoFilter的transform_frame_ip回调用于完成GstBuffer中frame的转换
filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_overlay_transform_frame_ip);
      
// gst_overlay_transform_frame_ip()调用
res = gst_overlay_apply_overlay (gst_overlay, frame);
/* <====================================================================== */

// ==============>qmmf_overlay.cc<===============//
// 绘制则是在ApplyOverlay()中的
// 首先判断OverlayItem->IsActive()
// 绘制
c2dDraw(target_c2dsurface_id_, 0, 0, 0, 0, c2d_objects.objects,
                numActiveOverlays);

```

可以看到在整个流程中流没有关于绘制线条thick有关的参数，而最终的绘制工作交给了C2D来完成：

```c++
// copybit_c2d.cpp
ctx->libc2d2 = ::dlopen("libC2D2.so", RTLD_NOW);
*(void **)&LINK_c2dDraw = ::dlsym(ctx->libc2d2, "c2dDraw");
```

这部分使用了动态库的显示调用，高通并没有开源C2D的源码，我在整个overlay相关的源码中都没有找到thick相关的参数，我原以为会开发一个宏定义，但是并没有，因此这部分调查告一段落。

**注：**在实际使用过程中，如果是绘制一直变化的bbox_info，绘制效果是能够接受的；但是测试过如果一直画同一个bbox_inffo，透明度会比较差，整个颜色比较淡。

**注：**`libqmmf_overlay.so`的实现依赖于C2D（GPU加速），这部分我并未深入了解过，因此不做过多介绍。

**后记：**关于qtioverlay的介绍至此就结束了，插件或多或少有一些缺陷，开发也不可避免要妥协。好在尝试解决问题的过程始终是有趣的，但是还是希望高通能够更好的维护和完善自家平台的工具，让开发者有更好的开发体验。
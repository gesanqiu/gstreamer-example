/*
 * @Description: GStreamer overlay plugin.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-05-19 15:43:22
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-05-21 16:50:48
 */

#ifndef __GST_OVERLAY_H__
#define __GST_OVERLAY_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/videofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY              (gst_overlay_get_type())
#define GST_OVERLAY(obj)              (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_OVERLAY, GstOverlay))
#define GST_OVERLAY_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_OVERLAY, GstOverlayClass))
#define GST_IS_OVERLAY(obj)           (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_OVERLAY))
#define GST_OVERLAY_CAST(obj)         ((GstOverlay *)(obj))

typedef enum _BufferFormat {
    NV12,
    NV21
}BufferFormat;

typedef struct _GstOverlay      GstOverlay;
typedef struct _GstOverlayClass GstOverlayClass;
typedef struct _GstOverlayText  GstOverlayText;
typedef struct _GstOverlayBBox  GstOverlayBBox;

struct _GstOverlay
{
    GstVideoFilter      parent;
    Overlay             *overlay;
    BufferFormat        format;
    guint               width;
    guint               height;
    GMutex              lock;

    /* User specified color */
    guint               bbox_color;
    guint               text_color;

    /* User specified overlay */
    GstOverlayText      *usr_text;
    GstOverlayBBox      *usr_bbox;
};

struct _GstOverlayClass {
    GstVideoFilterClass parent;
};

/* GstOverlayText - parameters for text overlay
 * text: user text
 * color: RGBA format overlay color
 * thick: user text box overlay thcik
 * left: left coordinate of text
 * top: top coordinate of text
 */
struct _GstOverlayText {
    gchar               *text;
    guint               color;
    guint               thick;
    guint               left;
    guint               top;
};

/* GstOverlayBBox - parameters for user bounding box overlay
 * label: bounding box label
 * boundind_box: boundind box rectangle
 * color: RGBA format overlay color
 * thick: bounding box overlay thcik
 */
struct _GstOverlayBBox {
    gchar               *label;
    GstVideoRectangle   bounding_box;
    guint               color;
    guint               thick;
};

G_GNUC_INTERNAL GType gst_overlay_get_type(void);

#define IS_OVERLAY_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

G_END_DECLS

#endif /* __GST_OVERLAY_H__ */

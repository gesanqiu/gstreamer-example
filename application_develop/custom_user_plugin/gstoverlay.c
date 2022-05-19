/*
 * @Description: GStreamer overlay plugin.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-05-19 15:43:22
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-05-19 17:32:04
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/allocators/gstfdmemory.h>

#include "gstoverlay.h"

#define GST_CAT_DEFAULT overlay_debug
GST_DEBUG_CATEGORY_STATIC (overlay_debug);

#define gst_overlay_parent_class parent_class
G_DEFINE_TYPE (GstOverlay, gst_overlay, GST_TYPE_VIDEO_FILTER);

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

#define GST_VIDEO_FORMATS "{ NV12, NV21 }"

// Default value of plugin properties
#define DEFAULT_PROP_OVERLAY_TEXT           "Hello, world!"
#define DEFAULT_PROP_OVERLAY_BBOX_X         10
#define DEFAULT_PROP_OVERLAY_BBOX_Y         10
#define DEFAULT_PROP_OVERLAY_TEXT_THICK     0x00FF00FF    // green
#define DEFAULT_PROP_OVERLAY_TEXT_COLOR     10
#define DEFAULT_PROP_OVERLAY_BBOX_X         NULL
#define DEFAULT_PROP_OVERLAY_BBOX_Y         NULL
#define DEFAULT_PROP_OVERLAY_BBOX_WIDTH     NULL
#define DEFAULT_PROP_OVERLAY_BBOX_HEIGHT    NULL
#define DEFAULT_PROP_OVERLAY_BBOX_COLOR     0xFF0000FF    // red
#define DEFAULT_PROP_OVERLAY_BBOX_THICK     10

/* Supported GST properties
 * PROP_OVERLAY_TEXT - overlays user defined text
 * PROP_OVERLAY_TEXT_COLOR - overlays text color
 * PROP_OVERLAY_TEXT_POSITION - user defined text position, e.g: (x,y)
 * PROP_OVERLAY_BBOX - overlays user defined bounding box
 * PROP_OVERLAY_BBOX_COLOR - overlays bounding box color
 * PROP_OVERLAY_TEXT_POSITION - user defined bounding box position, e.g: (x,y,width,height)
 */
enum {
    PROP_0,
    PROP_OVERLAY_TEXT,
    PROP_OVERLAY_TEXT_COLOR,
    PROP_OVERLAY_TEXT_POSITION,
    PROP_OVERLAY_BBOX,
    PROP_OVERLAY_BBOX_COLOR,
    PROP_OVERLAY_BBOX_POSITION
};

static GstStaticCaps gst_overlay_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));

static gboolean plugin_init(GstPlugin * plugin)
{
    return gst_element_register (plugin, "rloverlay", GST_RANK_PRIMARY,
            GST_TYPE_OVERLAY);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    rloverlay,
    "Simple open-source GStreamer plugin for overlay.",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)


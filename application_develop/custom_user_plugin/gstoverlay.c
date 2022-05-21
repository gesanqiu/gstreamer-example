/*
 * @Description: GStreamer overlay plugin.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-05-19 15:43:22
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-05-21 17:14:03
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
#define DEFAULT_PROP_OVERLAY_TEXT           NULL
#define DEFAULT_PROP_OVERLAY_BBOX_X         10
#define DEFAULT_PROP_OVERLAY_BBOX_Y         10
#define DEFAULT_PROP_OVERLAY_TEXT_COLOR     0x00FF00FF    // green
#define DEFAULT_PROP_OVERLAY_TEXT_THICK     8
#define DEFAULT_PROP_OVERLAY_BBOX_LABEL     NULL
#define DEFAULT_PROP_OVERLAY_BBOX_X         10
#define DEFAULT_PROP_OVERLAY_BBOX_Y         18
#define DEFAULT_PROP_OVERLAY_BBOX_WIDTH     100
#define DEFAULT_PROP_OVERLAY_BBOX_HEIGHT    100
#define DEFAULT_PROP_OVERLAY_BBOX_COLOR     0xFF0000FF    // red
#define DEFAULT_PROP_OVERLAY_BBOX_THICK     8

/* Supported GST properties
 * PROP_OVERLAY_TEXT - overlays user defined texts
 * PROP_OVERLAY_TEXT_COLOR - overlays text color
 * PROP_OVERLAY_TEXT_POSITION - user defined text position, e.g: (left,top)
 * PROP_OVERLAY_BBOX - overlays user defined bounding box position, e.g: (left,top,width,height)
 * PROP_OVERLAY_BBOX_COLOR - overlays bounding box color
 */
enum {
    PROP_0,
    PROP_OVERLAY_TEXT,
    PROP_OVERLAY_TEXT_COLOR,
    PROP_OVERLAY_TEXT_POSITION,
    PROP_OVERLAY_TEXT_THICK,
    PROP_OVERLAY_BBOX,
    PROP_OVERLAY_BBOX_COLOR,
    PROP_OVERLAY_BBOX_THICK
};

static void gst_overlay_set_property(GObject *object, guint prop_id,
    const GValue *value, GParamSpec *pspec)
{
    GstOverlay *gst_overlay = GST_OVERLAY(object);
    const gchar *propname = g_param_spec_get_name(pspec);
    GstState state = GST_STATE(gst_overlay);

    if (!OVERLAY_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
        GST_WARNING ("Property '%s' change not supported in %s state!",
            propname, gst_element_state_get_name (state));
        return;
    }

    GST_OBJECT_LOCK(gst_overlay);
    switch (prop_id) {
    case PROP_OVERLAY_TEXT:
        gst_overlay->text = g_strdup(g_value_get_strint(value));
        break;
    case PROP_OVERLAY_TEXT_COLOR:
        gst_overlay->text_color = g_value_get_uint(value);
        break;
    case PROP_OVERLAY_TEXT_POSITION:
        if (gst_value_array_get_size(value) != 2) {
            GST_DEBUG_OBJECT(gst_overlay, "text-position property is not set. Use default values.");
            break;
        }

        gst_overlay->usr_text->left =
            g_value_get_int(gst_value_array_get_value(value, 0));
        gst_overlay->usr_text->top =
            g_value_get_int(gst_value_array_get_value(value, 1));
        break;
    case PROP_OVERLAY_TEXT_THICK:
        gst_overlay->text_thick = g_value_get_uint(value);
        break;
    case PROP_OVERLAY_BBOX:
        if (gst_value_array_get_size(value) != 4) {
            GST_DEBUG_OBJECT(gst_overlay, "bbox property is not set. Use default values.");
            break;
        }

        gst_overlay->usr_bbox->bounding_box.x =
            g_value_get_int(gst_value_array_get_value(value, 0));
        gst_overlay->usr_bbox->bounding_box.y =
            g_value_get_int(gst_value_array_get_value(value, 1));
        gst_overlay->usr_bbox->bounding_box.w =
            g_value_get_int(gst_value_array_get_value(value, 2));
        gst_overlay->usr_bbox->bounding_box.h =
            g_value_get_int(gst_value_array_get_value(value, 3));
        break;
    case PROP_OVERLAY_BBOX_COLOR:
        gst_overlay->usr_bbox->bbox_color = g_value_get_uint(value);
        break;
    case PROP_OVERLAY_BBOX_THICK:
        gst_overlay->usr_bbox->bbox_thick = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }

    GST_OBJECT_UNLOCK(gst_overlay);
}

static void gst_overlay_get_property(GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
    GstOverlay *gst_overlay = GST_OVERLAY(object);

    GST_OBJECT_LOCK(gst_overlay);
    switch (prop_id) {
    case PROP_OVERLAY_TEXT:

        break;
    case PROP_OVERLAY_TEXT_COLOR:
        g_value_set_uint(value, gst_overlay->usr_text->text_color);
        break;
    case PROP_OVERLAY_TEXT_POSITION:
        /* TO-DO: bbox to string*/
        GValue val = G_VALUE_INIT;
        g_value_init (&val, G_TYPE_INT);

        g_value_set_int (&val, gst_overlay->usr_text->left);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, gst_overlay->usr_text->top);
        gst_value_array_append_value (value, &val);
        break;
    case PROP_OVERLAY_TEXT_THICK:
        g_value_set_uint(value, gst_overlay->usr_text->thick);
        break;
    case PROP_OVERLAY_BBOX:
        /* TO-DO: bbox to string*/
        GValue val = G_VALUE_INIT;
        g_value_init (&val, G_TYPE_INT);

        g_value_set_int (&val, gst_overlay->usr_bbox->bounding_box.x);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, gst_overlay->usr_bbox->bounding_box.y);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, gst_overlay->usr_bbox->bounding_box.w);
        gst_value_array_append_value (value, &val);

        g_value_set_int (&val, gst_overlay->usr_bbox->bounding_box.h);
        gst_value_array_append_value (value, &val);
        break;
    case PROP_OVERLAY_BBOX_COLOR:
        g_value_set_uint(value, gst_overlay->usr_bbox->bbox_color);
        break;
    case PROP_OVERLAY_BBOX_THICK:
        g_value_set_uint(value, gst_overlay->usr_bbox->thick);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }

    GST_OBJECT_UNLOCK(gst_overlay);
}

static GstStaticCaps gst_overlay_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));


static void gst_overlay_finalize(GObject *object)
{
    GstOverlay *gst_overlay = GST_OVERLAY(object);

    if (gst_overlay->overlay) {
        free(gst_overlay->overlay);
        gst_overlay->overlay = NULL;
    }

    if (gst_overlay->usr_text) {
        if (gst_overlay->usr_text->text) {
            free(gst_overlay->usr_text->text);
            gst_overlay->usr_text->text = NULL;
        }

        free(gst_overlay->usr_text);
        gst_overlay->usr_text = NULL;
    }

    if (gst_overlay->usr_bbox) {
        free(gst_overlay->usr_bbox);
        gst_overlay->usr_bbox = NULL;
    }

    g_mutex_clear (&gst_overlay->lock);

    G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (gst_overlay));
}

static gboolean gst_overlay_set_info(GstVideoFilter *filter, 
    GstCaps* in, GstVideoInfo *in_info
    GstCaps *out, GstVideoInfo *out_info)
{
    GstOverlay *gst_overlay = GST_OVERLAY(filter);
    BufferFormat  new_format;

    gst_base_transform_set_passthrough(GST_BASE_TRANSFORM (filter), FALSE);

    gst_overlay->width = GST_VIDEO_INFO_WIDTH(in_info);
    gst_overlay->height = GST_VIDEO_INFO_HEIGHT(in_info);

    switch (GST_VIDEO_INFO_FORMAT(in_info)) { // GstVideoFormat
    case GST_VIDEO_FORMAT_NV12:
        new_format = NV12;
        break;
    case GST_VIDEO_FORMAT_NV21:
        new_format = NV21;
        break;
    default:
        GST_ERROR_OBJECT(gst_overlay, "Unhandled gst format: %d",
            GST_VIDEO_INFO_FORMAT(in_info));
        return FALSE;
    }

    if (gst_overlay->overlay && gst_overlay->format == new_format) {
        GST_DEBUG_OBJECT(gst_overlay, "Overlay already initialized");
        return TRUE;
    }

    if (gst_overlay->overlay) {
        free(gst_overlay->overlay);
    }

    gst_overlay->format = new_format;
    gst_overlay->overlay = (Overlay*)malloc(sizeof(Overlay));

    int32_t ret = gst_overlay->overlay->Init(gst_overlay->format);
    if (ret != 0) {
        GST_ERROR_OBJECT (gst_overlay, "Overlay init failed! Format: %u",
            (guint)gst_overlay->format);
        free(gst_overlay->overlay);
        gst_overlay->overlay = NULL;
        return FALSE;
    }

    return TRUE;
}

static GstFlowReturn gst_overlay_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame)
{
    GstOverlay *gst_overlay = GST_OVERLAY_CAST(filter);
    gboolean res = TRUE;

    if (!gst_overlay->overlay) {
        GST_ERROR_OBJECT(gst_overlay, "failed: overlay not initialized");
        return GST_FLOW_ERROR;
    }

    /* TO-DO: overlay */

    if (!res) {
        GST_ERROR_OBJECT (gst_overlay, "Overlay apply failed!");
        return GST_FLOW_ERROR;
    }

    return GST_FLOW_OK;
}

static void gst_overlay_init(GstOverlay *gst_overlay)
{
    gst_overlay->overlay = NULL;
    gst_overlay->text_color = DEFAULT_PROP_OVERLAY_TEXT_COLOR;
    gst_overlay->bbox_color = DEFAULT_PROP_OVERLAY_BBOX_COLOR;

    gst_overlay->usr_text = (GstOverlayText*) malloc(sizeof(GstOverlayText));
    gst_overlay->usr_text->text = DEFAULT_PROP_OVERLAY_TEXT;
    gst_overlay->usr_text->color = DEFAULT_PROP_OVERLAY_TEXT_COLOR;
    gst_overlay->usr_text->left = DEFAULT_PROP_OVERLAY_BBOX_X;
    gst_overlay->usr_text->top = DEFAULT_PROP_OVERLAY_BBOX_Y;
    gst_overlay->usr_text->thick = DEFAULT_PROP_OVERLAY_TEXT_THICK;

    gst_overlay->usr_bbox = (GstOverlayBBox*) malloc(sizeof(GstOverlayBBox));
    gst_overlay->usr_bbox->label = DEFAULT_PROP_OVERLAY_BBOX_LABEL;
    gst_overlay->usr_bbox->color = DEFAULT_PROP_OVERLAY_BBOX_COLOR;
    gst_overlay->usr_bbox->thick = DEFAULT_PROP_OVERLAY_BBOX_THICK;
    gst_overlay->usr_bbox->bounding_box.x = DEFAULT_PROP_OVERLAY_BBOX_X;
    gst_overlay->usr_bbox->bounding_box.y = DEFAULT_PROP_OVERLAY_BBOX_Y;
    gst_overlay->usr_bbox->bounding_box.w = DEFAULT_PROP_OVERLAY_BBOX_WIDTH;
    gst_overlay->usr_bbox->bounding_box.h = DEFAULT_PROP_OVERLAY_BBOX_HEIGHT;

    g_mutex_init(&gst_overlay->lock);

    GST_DEBUG_CATEGORY_INIT(overlay_debug, "rloverlay", 0, "Simple overlay");
}

static void gst_overlay_class_init(GstOverlayClass *klass)
{
    GObjectClass *gobject       = G_OBJECT_CLASS(klass);
    GstElementClass *element    = GST_ELEMENT_CLASS(klass);
    GstVideoFilterClass *filter = GST_VIDEO_FILTER_CLASS(klass);

    /* define virtual function pointers */
    gobject->set_property = GST_DEBUG_FUNCPTR(gst_overlay_set_property);
    gobject->get_property = GST_DEBUG_FUNCPTR(gst_overlay_get_property);
    gobject->finalize     = GST_DEBUG_FUNCPTR(gst_overlay_finalize);

    /* define properties */
    g_object_class_install_property(object_class, PROP_OVERLAY_TEXT,
        g_param_spec_string ("text", "Overlay text.",
            "Renders text on top of video stream.",
            DEFAULT_PROP_OVERLAY_TEXT, G_PARAM_CONSTRUCT | G_PARAM_READWRITE | 
            G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));

    g_object_class_install_property (gobject, PROP_OVERLAY_TEXT_COLOR,
        g_param_spec_uint ("text-color", "Text color", "Text overlay color in RGBA format.",
            0xFF, G_MAXUINT, DEFAULT_PROP_OVERLAY_TEXT_COLOR, 
            G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_OVERLAY_TEXT_POSITION,
        g_param_spec_string ("text-position", "Text position.",
            "Renders text on top of video stream at specified position.",
            "(10,10)", G_PARAM_CONSTRUCT | G_PARAM_READWRITE | 
            G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));

    g_object_class_install_property (gobject, PROP_OVERLAY_TEXT_THICK,
        g_param_spec_uint ("text-thcik", "Text thick", "Text overlay thick.",
            0, 50, DEFAULT_PROP_OVERLAY_TEXT_THICK, G_PARAM_CONSTRUCT | 
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property(object_class, PROP_OVERLAY_BBOX,
        g_param_spec_string ("bbox", "Overlay bbox.",
            "Renders bbox on top of video stream at specified position.",
            "(10,10,100,100)", G_PARAM_CONSTRUCT | G_PARAM_READWRITE | 
            G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_PLAYING));

    g_object_class_install_property (gobject, PROP_OVERLAY_BBOX_COLOR,
        g_param_spec_uint ("bbox-color", "BBox color", "Bounding box overlay color in RGBA format.",
            0xFF, G_MAXUINT, DEFAULT_PROP_OVERLAY_BBOX_COLOR, 
            G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    g_object_class_install_property (gobject, PROP_OVERLAY_BBOX_THICK,
        g_param_spec_uint ("bbox-thick", "BBox thick", "Bounding box overlay thick.",
            0, 50, DEFAULT_PROP_OVERLAY_BBOX_THICK, G_PARAM_CONSTRUCT | 
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    gst_element_class_set_static_metadata(gobject,
        "An example plugin",
        "Overlay",
        "Simple open-source GStreamer plugin for overlay.",
        "Ricardo Lu<shenglu1202@163.com>");

    /* define pads */
    gst_element_class_add_pad_template(element, gst_overlay_sink_template());
    gst_element_class_add_pad_template(element, gst_overlay_src_template());

    filter->set_info = GST_DEBUG_FUNCPTR(gst_overlay_set_info);
    filter->transform_frame_ip = GST_DEBUG_FUNCPTR(gst_overlay_transform_frame_ip);
}

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


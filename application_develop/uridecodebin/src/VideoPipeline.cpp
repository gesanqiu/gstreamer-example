/*
 * @Description: Implement of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:01:39
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-09-03 23:32:44
 */

#include "VideoPipeline.h"

static void cb_qtdemux_pad_added (
    GstElement* src, GstPad* new_pad, gpointer user_data)
{
    GstPadLinkReturn ret;
    GstCaps*         new_pad_caps = NULL;
    GstStructure*    new_pad_struct = NULL;
    const gchar*     new_pad_type = NULL;

    LOG_INFO_MSG ("cb_qtdemux_pad_added called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    GstPad* v_sinkpad = gst_element_get_static_pad (
                    reinterpret_cast<GstElement*> (vp->m_h264parse), "sink");

    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);

    if (!g_str_has_prefix (new_pad_type, "video/x-h264")) {
        LOG_WARN_MSG ("It has type '%s' which is not raw video. Ignoring.",
            new_pad_type);
        goto exit;
    }

    /* Attempt the link */
    ret = gst_pad_link (new_pad, v_sinkpad);
    if (GST_PAD_LINK_FAILED (ret)) {
        LOG_ERROR_MSG ("fail to link qtdemux and h264parse");
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    /* Unreference the sink pad */
    gst_object_unref (v_sinkpad);
}

/* 
 * This function is called when decodebin has created the decode element, 
 * filesrc: qtdemux, multiqueue, h264parse/h265parse, capfilter, qtivdec
 * rtspsrc: rtph264depay/rtph265depay, h264parse/h265parse, capfilter, qtivdec
 * so we have chance to configure it.
 */
static void
cb_decodebin_child_added (
    GstChildProxy* child_proxy, GObject* object, gchar* name, gpointer user_data)
{
    LOG_INFO_MSG ("cb_decodebin_child_added called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    LOG_INFO_MSG ("Element '%s' added to decodebin", name);

    if (g_strrstr (name, "qtdemux") == name) {
        vp->m_demux = reinterpret_cast<GstElement*> (object);
        /*
         * can't get h264parse sink pad and throw segement fault
         * guess decodebin create h264parse in another thread
         * and we can't get info of it at this time
         */
        // g_signal_connect (G_OBJECT (object), "pad-added",
        //     G_CALLBACK (cb_qtdemux_pad_added), vp);
    }

    if ((g_strrstr (name, "h264parse") == name)) {
        vp->m_h264parse = reinterpret_cast<GstElement*> (object);
        LOG_INFO_MSG ("h264parse address: %p", vp->m_h264parse);
    }

    if (g_strrstr (name, "qtivdec") == name) {
        vp->m_vdecoder = reinterpret_cast<GstElement*> (object);
        g_object_set (object, "turbo", vp->m_config.turbo, NULL);
        g_object_set (object, "skip-frames", vp->m_config.skip_frame, NULL);
    }
}

/* 
 * This function is called when uridecodebin has created 
 * the source element: filesrc/rtspsrc/appsrc
 * so we have chance to configure it.
 */
static void cb_uridecodebin_source_setup (
    GstElement* pipeline, GstElement* source, gpointer user_data)
{
    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    LOG_INFO_MSG ("cb_uridecodebin_source_setup called");

    /* Configure rtspsrc
    if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "latency")) {
        LOG_INFO_MSG ("cb_uridecodebin_source_setup set %d latency",
            vp->m_config.rtsp_latency);
        g_object_set (G_OBJECT (source), "latency", vp->m_config.rtsp_latency, NULL);
    }
    */

   /* Configure appsrc
    GstCaps *m_sCaps;
    src_Caps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.src_format.c_str(), "width", G_TYPE_INT, m_config.src_width,
          "height", G_TYPE_INT, m_config.src_height, NULL);
    g_object_set (G_OBJECT(source), "caps", src_Caps, NULL);
    g_signal_connect (source, "need-data", G_CALLBACK (start_feed), data);
    g_signal_connect (source, "enough-data", G_CALLBACK (stop_feed), data);
    gst_caps_unref (src_Caps);
   */
}

static void cb_uridecodebin_pad_added (
    GstElement* src, GstPad* new_pad, gpointer user_data)
{
    LOG_INFO_MSG ("cb_uridecodebin_pad_added called");

    GstPadLinkReturn ret;
    GstCaps*         new_pad_caps = NULL;
    GstStructure*    new_pad_struct = NULL;
    const gchar*     new_pad_type = NULL;
    GstPad*          v_sinkpad = NULL;
    GstPad*          a_sinkpad = NULL;

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    new_pad_caps = gst_pad_get_current_caps (new_pad);
    new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
    new_pad_type = gst_structure_get_name (new_pad_struct);

    if (g_str_has_prefix (new_pad_type, "video/x-raw")) {
        LOG_WARN_MSG ("Linking video/x-raw");
        /* Attempt the link */
        v_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_display), "sink");
        ret = gst_pad_link (new_pad, v_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link video source with waylandsink");
            goto exit;
        }
    } else if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
        a_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_audioConv), "sink");
        ret = gst_pad_link (new_pad, a_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link audio source and audioconvert");
            goto exit;
        }
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    /* Unreference the sink pad */
    if (v_sinkpad) gst_object_unref (v_sinkpad);
    if (a_sinkpad) gst_object_unref (a_sinkpad);
}

static void cb_uridecodebin_child_added (
    GstChildProxy* child_proxy, GObject* object, gchar* name, gpointer user_data)
{
    LOG_INFO_MSG ("cb_uridecodebin_child_added called");

    LOG_INFO_MSG ("Element '%s' added to uridecodebin", name);
    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    if (g_strrstr (name, "decodebin") == name) {
        g_signal_connect (G_OBJECT (object), "child-added",
            G_CALLBACK (cb_decodebin_child_added), vp);
    }
}

VideoPipeline::VideoPipeline (const VideoPipelineConfig& config)
{
    m_config = config;
}

VideoPipeline::~VideoPipeline ()
{
    Destroy ();
}

bool VideoPipeline::Create (void)
{
    if (!(m_gstPipeline = gst_pipeline_new ("video-pipeline"))) {
        LOG_ERROR_MSG ("Failed to create pipeline named video-pipeline");
        goto exit;
    }
    gst_pipeline_set_auto_flush_bus (GST_PIPELINE (m_gstPipeline), true);

    if (!(m_source = gst_element_factory_make ("uridecodebin", "src"))) {
        LOG_ERROR_MSG ("Failed to create element uridecodebin named src");
        goto exit;
    }
    g_object_set (G_OBJECT (m_source), "uri", m_config.src.c_str(), NULL);

    g_signal_connect (G_OBJECT (m_source), "source-setup", G_CALLBACK (
        cb_uridecodebin_source_setup), reinterpret_cast<void*> (this));
    g_signal_connect (G_OBJECT (m_source), "pad-added",    G_CALLBACK (
        cb_uridecodebin_pad_added),    reinterpret_cast<void*> (this));
    g_signal_connect (G_OBJECT (m_source), "child-added",  G_CALLBACK (
        cb_uridecodebin_child_added),  reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_gstPipeline), m_source, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_display, NULL);

    if (!(m_audioConv = gst_element_factory_make ("audioconvert", "audioconv"))) {
        LOG_ERROR_MSG ("Failed to create element audioconvert named audioconv");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_audioConv, NULL);

    if (!(m_audioReSample = gst_element_factory_make ("audioresample", "resample"))) {
        LOG_ERROR_MSG ("Failed to create element audioresample named resample");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_audioReSample, NULL);

    if (!(m_player = gst_element_factory_make ("pulsesink", "player"))) {
        LOG_ERROR_MSG ("Failed to create element plusesink named player");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_player, NULL);

    g_object_set (G_OBJECT (m_player), "volumn", 1.0, NULL);

    return true;

exit:
    LOG_ERROR_MSG ("Failed to create video pipeline");
    return false;
}

bool VideoPipeline::Start (void)
{
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (m_gstPipeline,
        GST_STATE_PLAYING)) {
        LOG_ERROR_MSG ("Failed to set pipeline to playing state");
        return false;
    }

    return true;
}

bool VideoPipeline::Pause (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StopPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_gstPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PAUSED) {
        return true;
    } else if (state == GST_STATE_PLAYING) {
        gst_element_set_state (m_gstPipeline, GST_STATE_PAUSED);
        gst_element_get_state (m_gstPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

bool VideoPipeline::Resume (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StartPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_gstPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PLAYING) {
        return true;
    } else if (state == GST_STATE_PAUSED) {
        gst_element_set_state (m_gstPipeline, GST_STATE_PLAYING);
        gst_element_get_state (m_gstPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

void VideoPipeline::Destroy (void)
{
    if (m_gstPipeline) {
        gst_element_set_state (m_gstPipeline, GST_STATE_NULL);

        gst_object_unref (m_gstPipeline);

        m_gstPipeline = NULL;
    }
}

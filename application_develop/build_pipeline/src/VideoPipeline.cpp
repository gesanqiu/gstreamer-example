/*
 * @Description: Implement of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:01:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-31 14:07:18
 */

#include "VideoPipeline.h"

static void qtdemux_pad_added_cb (
    GstElement* src, GstPad* new_pad, gpointer user_data)
{
    GstPadLinkReturn ret;
    GstCaps*         new_pad_caps = NULL;
    GstStructure*    new_pad_struct = NULL;
    const gchar*     new_pad_type = NULL;

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

VideoPipeline::VideoPipeline (const VideoPipelineConfig& config)
{
    m_config = config;
}

VideoPipeline::~VideoPipeline ()
{
    Destroy ();
}

#ifdef PARSE_LAUNCH
bool VideoPipeline::SetPipeline (std::string& pipeline)
{
    m_pipeline = pipeline;
}
#endif

bool VideoPipeline::Create (void)
{

#ifdef PARSE_LAUNCH
    GError *error = NULL;

    LOG_INFO_MSG ("Parsing pipeline: %s", m_pipeline.c_str());
    m_gstPipeline = gst_parse_launch (m_pipeline.c_str(), &error);
    if ( error != NULL ) {
        LOG_ERROR_MSG ("Could not construct pipeline: %s", error->message);
        g_clear_error (&error);
        goto exit;
    }

    return true;
#endif

#ifdef FACTORY_MAKE
    if (!(m_gstPipeline = gst_pipeline_new ("video-pipeline"))) {
        LOG_ERROR_MSG ("Failed to create pipeline named video-pipeline");
        goto exit;
    }
    gst_pipeline_set_auto_flush_bus (GST_PIPELINE (m_gstPipeline), true);

    if (!(m_source = gst_element_factory_make ("filesrc", "src"))) {
        LOG_ERROR_MSG ("Failed to create element filesrc named src");
        goto exit;
    }
    g_object_set (G_OBJECT (m_source), "location",
            m_config.src.c_str(), NULL);
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_source, NULL);

    if (!(m_qtdemux = gst_element_factory_make ("qtdemux", "demux"))) {
        LOG_ERROR_MSG ("Failed to create element qtdemux named demux");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_qtdemux, NULL);

    if (!gst_element_link_many (m_source, m_qtdemux, NULL)) {
        LOG_ERROR_MSG ("Failed to link filesrc->qtdemux");
            goto exit;
    }

    if (!(m_h264parse = gst_element_factory_make ("h264parse", "parse"))) {
        LOG_ERROR_MSG ("Failed to create element h264parse named parse");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_h264parse, NULL);
    
    // Link qtdemux with h264parse
    g_signal_connect (m_qtdemux, "pad-added",
        G_CALLBACK(qtdemux_pad_added_cb), reinterpret_cast<void*> (this));

    if (!(m_decoder = gst_element_factory_make ("qtivdec", "decode"))) {
        LOG_ERROR_MSG ("Failed to create element qtivdec named decode");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_decoder, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_display, NULL);

    if (!gst_element_link_many (m_h264parse, m_decoder, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link h264parse->qtivdec->waylandsink");
            goto exit;
    }

    return true;
#endif

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

/*
 * @Description: Implement of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:01:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-09-01 12:40:47
 */

#include "VideoPipeline.h"

#ifdef FACTORY_MAKE
static void cb_qtdemux_pad_added (
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

    if (g_str_has_prefix (new_pad_type, "video/x-h264")) {
        LOG_INFO_MSG ("Linking video/x-raw");
        /* Attempt the link */
        v_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_queue0), "sink");
        ret = gst_pad_link (new_pad, v_sinkpad);
        if (GST_PAD_LINK_FAILED (ret)) {
            LOG_ERROR_MSG ("fail to link video source with waylandsink");
            goto exit;
        }
    } else if (g_str_has_prefix (new_pad_type, "audio/mpeg")) {
        LOG_INFO_MSG ("Linking audio/x-raw");
        a_sinkpad = gst_element_get_static_pad (
                        reinterpret_cast<GstElement*> (vp->m_queue1), "sink");
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
#endif

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

    if (!(m_queue0 = gst_element_factory_make ("queue", "queue0"))) {
        LOG_ERROR_MSG ("Failed to create element queue named queue0");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_queue0, NULL);

    if (!(m_queue1 = gst_element_factory_make ("queue", "queue1"))) {
        LOG_ERROR_MSG ("Failed to create element queue named queue1");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_queue1, NULL);

    if (!gst_element_link_many (m_source, m_qtdemux, NULL)) {
        LOG_ERROR_MSG ("Failed to link filesrc->qtdemux");
        goto exit;
    }

    if (!(m_h264parse = gst_element_factory_make ("h264parse", "vparse"))) {
        LOG_ERROR_MSG ("Failed to create element h264parse named vparse");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_h264parse, NULL);

    // Link qtdemux src-pad with queue
    g_signal_connect (m_qtdemux, "pad-added",
        G_CALLBACK(cb_qtdemux_pad_added), reinterpret_cast<void*> (this));

    if (!(m_vdecoder = gst_element_factory_make ("qtivdec", "vdecoder"))) {
        LOG_ERROR_MSG ("Failed to create element qtivdec named vdecoder");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_vdecoder, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_display, NULL);

    if (!gst_element_link_many (m_queue0, m_h264parse, m_vdecoder, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link queue->h264parse->qtivdec->waylandsink");
        goto exit;
    }

    if (!(m_aacparse = gst_element_factory_make ("aacparse", "aparse"))) {
        LOG_ERROR_MSG ("Failed to create element aacparse named aparse");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_aacparse, NULL);

    if (!(m_adecoder = gst_element_factory_make ("avdec_aac", "adecoder"))) {
        LOG_ERROR_MSG ("Failed to create element avdec_aac named adecoder");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_adecoder, NULL);

    if (!(m_audioConv = gst_element_factory_make ("audioconvert", "audioconv"))) {
        LOG_ERROR_MSG ("Failed to create element audioconvert named audioconv");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_audioConv, NULL);

    if (!(m_audioReSample = gst_element_factory_make ("audioresample", "aresample"))) {
        LOG_ERROR_MSG ("Failed to create element audioresample named aresample");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_audioReSample, NULL);

    if (!(m_player = gst_element_factory_make ("pulsesink", "player"))) {
        LOG_ERROR_MSG ("Failed to create element plusesink named player");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_player, NULL);

    g_object_set (G_OBJECT (m_player), "volume", 1.0, NULL);

    if (!gst_element_link_many (m_queue1, m_aacparse, m_adecoder, m_audioConv,
            m_audioReSample, m_player, NULL)) {
        LOG_ERROR_MSG ("Failed to link queue->aacparse->avdec_aac->audioconvert"
            "->audioresample->pulsesink");
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

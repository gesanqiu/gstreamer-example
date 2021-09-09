/*
 * @Description: Implement of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:01:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-09-09 13:21:20
 */

#include "VideoPipeline.h"

static GstPadProbeReturn cb_queue0_probe (
    GstPad* pad, 
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_queue0_probe called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    // osd the result
    if (vp->m_getResultFunc) {
        const std::shared_ptr<cv::Rect> result =
            vp->m_getResultFunc (vp->m_getResultArgs);
        if (result && vp->m_procDataFunc) {
            LOG_INFO_MSG ("probe buffer %s write", gst_buffer_is_writable (buffer) ? "can":"can't");
            vp->m_procDataFunc (buffer, result);
        }
    }

    // LOG_INFO_MSG ("cb_osd_buffer_probe exited");

    return GST_PAD_PROBE_OK;
}

static GstFlowReturn cb_appsink_new_sample (
    GstElement* appsink,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_appsink_new_sample called, user data: %p", user_data);

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);
    GstSample* sample = NULL;
    GstBuffer* buffer = NULL;
    GstMapInfo map;
    const GstStructure* info = NULL;
    GstCaps* caps = NULL;
    int sample_width = 0;
    int sample_height = 0;

    g_signal_emit_by_name (appsink, "pull-sample", &sample);

    if (sample) {
        buffer = gst_sample_get_buffer (sample);
        if ( buffer == NULL ) {
            LOG_ERROR_MSG ("get buffer is null");
            goto exit;
        }

        gst_buffer_map (buffer, &map, GST_MAP_READ);

        caps = gst_sample_get_caps (sample);
        if ( caps == NULL ) {
            LOG_ERROR_MSG ("get caps is null");
            goto exit;
        }

        info = gst_caps_get_structure (caps, 0);
        if ( info == NULL ) {
            LOG_ERROR_MSG ("get info is null");
            goto exit;
        }

        // ---- Read frame and convert to opencv format ---------------
        // convert gstreamer data to OpenCV Mat, you could actually
        // resolve height / width from caps...
        gst_structure_get_int (info, "width", &sample_width);
        gst_structure_get_int (info, "height", &sample_height);

        // appsink product queue produce
        {
            // init a cv::Mat with gst buffer address: deep copy
            if (map.data == NULL) {
                LOG_ERROR_MSG("appsink buffer data empty\n");
                return GST_FLOW_OK;
            }

            cv::Mat img (sample_height, sample_width, CV_8UC3,
                        (unsigned char*)map.data, cv::Mat::AUTO_STEP);
            img = img.clone();

            if (vp->m_putDataFunc) {
                vp->m_putDataFunc(std::make_shared<cv::Mat> (img),
                    vp->m_putDataArgs);
            } else {
                goto exit;
            }
        }
    }

exit:
    if (buffer) {
        gst_buffer_unmap (buffer, &map);
    }
    if (sample) {
        gst_sample_unref (sample);
    }
    return GST_FLOW_OK;
}

static void cb_qtdemux_pad_added (
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

bool VideoPipeline::Create (void)
{
    GstCaps* m_transCaps;
    GstPad *m_gstPad;

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
        G_CALLBACK(cb_qtdemux_pad_added), reinterpret_cast<void*> (this));

    if (!(m_decoder = gst_element_factory_make ("qtivdec", "decode"))) {
        LOG_ERROR_MSG ("Failed to create element qtivdec named decode");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_decoder, NULL);

    if (!(m_tee = gst_element_factory_make ("tee", "t1"))) {
        LOG_ERROR_MSG ("Failed to create element tee named t1");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_tee, NULL);

    if (!(m_queue0 = gst_element_factory_make ("queue", "queue0"))) {
        LOG_ERROR_MSG ("Failed to create element queue named queue0");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_queue0, NULL);

    // add probe to queue0
    m_gstPad = gst_element_get_static_pad (m_queue0, "src");
    m_queue0_probe = gst_pad_add_probe (m_gstPad, (GstPadProbeType) (
                        GST_PAD_PROBE_TYPE_BUFFER), cb_queue0_probe, this, NULL);
    gst_object_unref (m_gstPad);

    if (!(m_qtioverlay = gst_element_factory_make ("qtioverlay", "overlay"))) {
        LOG_ERROR_MSG ("Failed to create element qtioverlay named overlay");
        goto exit;
    }
    g_object_set (G_OBJECT (m_qtioverlay), "meta-color", true, NULL);
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_qtioverlay, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_display, NULL);

    if (!gst_element_link_many (m_h264parse, m_decoder, m_tee, 
            m_queue0, m_qtioverlay, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link h264parse->qtivdec"
            "->tee->queue0->waylandsink");
        goto exit;
    }

    if (!(m_queue1 = gst_element_factory_make ("queue", "queue1"))) {
        LOG_ERROR_MSG ("Failed to create element queue named queue1");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_queue1, NULL);

    if (!(m_qtivtrans = gst_element_factory_make ("qtivtransform", "transform"))) {
        LOG_ERROR_MSG ("Failed to create element qtivtransform named transform");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_qtivtrans, NULL);

    m_transCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.conv_format.c_str(), "width", G_TYPE_INT, m_config.conv_width,
        "height", G_TYPE_INT, m_config.conv_height, NULL);

    if (!(m_capfilter = gst_element_factory_make("capsfilter", "capfilter"))) {
        LOG_ERROR_MSG ("Failed to create element capsfilter named capfilter");
        goto exit;
    }

    g_object_set (G_OBJECT(m_capfilter), "caps", m_transCaps, NULL);
    gst_caps_unref (m_transCaps);

    gst_bin_add_many (GST_BIN (m_gstPipeline), m_capfilter, NULL);

    if (!(m_appsink = gst_element_factory_make ("appsink", "appsink"))) {
        LOG_ERROR_MSG ("Failed to create element appsink named appsink");
        goto exit;
    }

    g_object_set (m_appsink, "emit-signals", TRUE, NULL);

    g_signal_connect (m_appsink, "new-sample",
        G_CALLBACK (cb_appsink_new_sample), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_gstPipeline), m_appsink, NULL);

    if (!gst_element_link_many (m_tee, m_queue1, m_qtivtrans, 
            m_capfilter, m_appsink, NULL)) {
        LOG_ERROR_MSG ("Failed to link tee->queue1->"
            "qtivtransform->capfilter->appsink");
        goto exit;
    }

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

void VideoPipeline::SetCallbacks (SinkPutDataFunc func, void* args)
{
    LOG_INFO_MSG ("set pudata callback called");

    m_putDataFunc = func;
    m_putDataArgs = args;
}

void VideoPipeline::SetCallbacks (ProbeGetResultFunc func, void* args)
{
    LOG_INFO_MSG ("set getdata callback called");

    m_getResultFunc = func;
    m_getResultArgs = args;
}

void VideoPipeline::SetCallbacks (ProcDataFunc func, void* args)
{
    LOG_INFO_MSG ("set procdata callback called");

    m_procDataFunc = func;
    m_procDataArgs = args;
}
/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:57:03
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 11:50:25
 */

#include "appsink.h"

GstFlowReturn cb_appsink_new_sample (
    GstElement* sink,
    gpointer user_data)
{
    //TS_INFO_MSG_V ("cb_appsink_new_sample called");

    SinkPipeline* sp = (SinkPipeline*) user_data;
    GstSample* sample = NULL;

    g_signal_emit_by_name (sink, "pull-sample", &sample);

    if (sample) {
        if (sp->m_putDataFunc) {
            sp->m_putDataFunc(sample, sp->m_putDataArgs);
        } else {
            gst_sample_unref (sample);
        }
    }

    return GST_FLOW_OK;
}

static void qtdemux_pad_added_cb (
    GstElement* qtdemux, GstPad* pad, gpointer data)
{
    GstPad* sinkpad = gst_element_get_static_pad (
                        static_cast<GstElement*> (data), "sink");

    if (gst_pad_link(pad, sinkpad) != GST_PAD_LINK_OK) {
        LOG_ERROR_MSG ("fail to link qtdemux and h264parse");
    }
    gst_object_unref(sinkpad);
}

SinkPipeline::SinkPipeline (const SinkPipelineConfig& config)
{
    m_config = config;
}

SinkPipeline::~SinkPipeline ()
{
    Destroy ();
}

bool SinkPipeline::Create (void)
{
    GstCaps* m_transCaps;

    // decode pipeline
    if (!(m_sinkPipeline = gst_pipeline_new ("decode-pipeline"))) {
        LOG_ERROR_MSG ("Failed to create pipeline named decode-pipeline");
        goto exit;
    }
    gst_pipeline_set_auto_flush_bus (GST_PIPELINE (m_sinkPipeline), true);

    if (!(m_source = gst_element_factory_make ("filesrc", "src"))) {
        LOG_ERROR_MSG ("Failed to create element filesrc named src");
        goto exit;
    }
    g_object_set (G_OBJECT (m_source), "location",
            m_config.src.c_str(), NULL);
    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_source, NULL);

    if (!(m_qtdemux = gst_element_factory_make ("qtdemux", "demux"))) {
        LOG_ERROR_MSG ("Failed to create element qtdemux named demux");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_qtdemux, NULL);

    if (!gst_element_link_many (m_source, m_qtdemux, NULL)) {
        LOG_ERROR_MSG ("Failed to link filesrc->qtdemux");
            goto exit;
    }

    if (!(m_h264parse = gst_element_factory_make ("h264parse", "parse"))) {
        LOG_ERROR_MSG ("Failed to create element h264parse named parse");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_h264parse, NULL);
    
    // Link qtdemux with h264parse
    g_signal_connect (m_qtdemux, "pad-added",
        G_CALLBACK(qtdemux_pad_added_cb), m_h264parse);

    if (!(m_decoder = gst_element_factory_make ("qtivdec", "decode"))) {
        LOG_ERROR_MSG ("Failed to create element qtivdec named decode");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_decoder, NULL);

    if (!(m_qtivtrans = gst_element_factory_make ("qtivtransform", "transform"))) {
        LOG_ERROR_MSG ("Failed to create element qtivtransform named transform");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_qtivtrans, NULL);

    m_transCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.sink_format.c_str(), "width", G_TYPE_INT, m_config.sink_width,
          "height", G_TYPE_INT, m_config.sink_height, NULL);

    if (!(m_capfilter = gst_element_factory_make("capsfilter", "capfilter"))) {
        LOG_ERROR_MSG ("Failed to create element capsfilter named capfilter");
        goto exit;
    }

    g_object_set (G_OBJECT(m_capfilter), "caps", m_transCaps, NULL);
    gst_caps_unref (m_transCaps);

    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_capfilter, NULL);

    if (!(m_appsink = gst_element_factory_make ("appsink", "appsink"))) {
        LOG_ERROR_MSG ("Failed to create element appsink named appsink");
        goto exit;
    }

    g_object_set (m_appsink, "emit-signals", TRUE, NULL);

    g_signal_connect (m_appsink, "new-sample", G_CALLBACK (
        cb_appsink_new_sample), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_sinkPipeline), m_appsink, NULL);

    if (!gst_element_link_many (m_h264parse, m_decoder, m_qtivtrans,
            m_capfilter, m_appsink, NULL)) {
        LOG_ERROR_MSG ("Failed to link h264parse->qtivdec->"
            "qtivtransfrom->capfilter->appsink");
            goto exit;
    }

    return true;

exit:
    LOG_ERROR_MSG ("Failed to create video pipeline");
    return false;
}

bool SinkPipeline::Start (void)
{
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (m_sinkPipeline,
        GST_STATE_PLAYING)) {
        LOG_ERROR_MSG ("Failed to set decode pipeline to playing state");
        return false;
    }
    return true;
}

bool SinkPipeline::Pause (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StopPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_sinkPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of decode pipeline");
        return false;
    }

    if (state == GST_STATE_PAUSED) {
        return true;
    } else if (state == GST_STATE_PLAYING) {
        gst_element_set_state (m_sinkPipeline, GST_STATE_PAUSED);
        gst_element_get_state (m_sinkPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of decode pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

bool SinkPipeline::Resume (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StartPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_sinkPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of decode pipeline");
        return false;
    }

    if (state == GST_STATE_PLAYING) {
        return true;
    } else if (state == GST_STATE_PAUSED) {
        gst_element_set_state (m_sinkPipeline, GST_STATE_PLAYING);
        gst_element_get_state (m_sinkPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of decode pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

void SinkPipeline::Destroy (void)
{
    if (m_sinkPipeline) {
        gst_element_set_state (m_sinkPipeline, GST_STATE_NULL);

        gst_object_unref (m_sinkPipeline);

        m_sinkPipeline = NULL;
    }
}
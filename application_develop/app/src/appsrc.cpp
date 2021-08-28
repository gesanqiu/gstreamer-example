/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:57:13
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 11:58:32
 */

#include "appsrc.h"

GstFlowReturn cb_appsrc_need_data (
    GstElement* src,
    gpointer user_data)
{
    //TS_INFO_MSG_V ("cb_appsink_new_sample called");
    SrcPipeline* sp = (SrcPipeline*) user_data;

    return GST_FLOW_OK;
}

SrcPipeline::SrcPipeline (const SrcPipelineConfig& config)
{
    m_config = config;
}

SrcPipeline::~SrcPipeline ()
{
    Destroy ();
}

bool SrcPipeline::Create (void)
{
    GstCaps* m_transCaps;

    // display pipeline
    if (!(m_srcPipeline = gst_pipeline_new ("display-pipeline"))) {
        LOG_ERROR_MSG ("Failed to create pipeline named display-pipeline");
        goto exit;
    }
    gst_pipeline_set_auto_flush_bus (GST_PIPELINE (m_srcPipeline), true);

    if (!(m_appsrc = gst_element_factory_make ("appsrc", "appsrc"))) {
        LOG_ERROR_MSG ("Failed to create element appsrc named appsrc");
        goto exit;
    }

    g_signal_connect (m_appsrc, "need-data",
        G_CALLBACK (cb_appsrc_need_data), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_srcPipeline), m_appsrc, NULL);
    
    if (!(m_videoconv = gst_element_factory_make ("videoconvert", "videoconv"))) {
        LOG_ERROR_MSG ("Failed to create element videoconvert named videoconv");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_srcPipeline), m_videoconv, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_srcPipeline), m_display, NULL);

    if (!gst_element_link_many (m_appsrc, m_videoconv, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link h264parse->qtivdec->waylandsink");
            goto exit;
    }

    return true;

exit:
    LOG_ERROR_MSG ("Failed to create video pipeline");
    return false;
}

bool SrcPipeline::Start (void)
{
    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state (m_srcPipeline,
        GST_STATE_PLAYING)) {
        LOG_ERROR_MSG ("Failed to set display pipeline to playing state");
        return false;
    }

    return true;
}

bool SrcPipeline::Pause (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StopPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_srcPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of display pipeline");
        return false;
    }

    if (state == GST_STATE_PAUSED) {
        return true;
    } else if (state == GST_STATE_PLAYING) {
        gst_element_set_state (m_srcPipeline, GST_STATE_PAUSED);
        gst_element_get_state (m_srcPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of display pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

bool SrcPipeline::Resume (void)
{
    GstState state, pending;

    LOG_INFO_MSG ("StartPipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state (
        m_srcPipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN_MSG ("Failed to get state of display pipeline");
        return false;
    }

    if (state == GST_STATE_PLAYING) {
        return true;
    } else if (state == GST_STATE_PAUSED) {
        gst_element_set_state (m_srcPipeline, GST_STATE_PLAYING);
        gst_element_get_state (m_srcPipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN_MSG ("Invalid state of display pipeline(%d)",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

void SrcPipeline::Destroy (void)
{
    if (m_srcPipeline) {
        gst_element_set_state (m_srcPipeline, GST_STATE_NULL);

        gst_object_unref (m_srcPipeline);

        m_srcPipeline = NULL;
    }
}
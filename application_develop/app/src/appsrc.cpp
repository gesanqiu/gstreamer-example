/*
 * @Description: Appsrc Pipeline Implement.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:57:13
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-31 09:12:02
 */

#include "appsrc.h"

GstFlowReturn cb_appsrc_need_data (
    GstElement* appsrc,
    guint length,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_appsrc_need_data called, user_data: %p", user_data);
    
    SrcPipeline* sp = reinterpret_cast<SrcPipeline*> (user_data);
    GstBuffer* buffer;
    GstMapInfo map;
    GstFlowReturn ret = GST_FLOW_OK;

    std::shared_ptr<cv::Mat> img;

    if (sp->m_getDataFunc) {
        img = sp->m_getDataFunc (sp->m_getDataArgs);

        int len = img->total() * img->elemSize();
        // zero-copy GstBuffer
        // buffer = gst_buffer_new_wrapped(img->data, len);
        buffer = gst_buffer_new_allocate (NULL, len, NULL);

        gst_buffer_map(buffer,&map,GST_MAP_READ);
        memcpy(map.data, img->data, len);

        GST_BUFFER_PTS (buffer) = sp->m_timestamp;
        GST_BUFFER_DURATION (buffer) = gst_util_uint64_scale_int (1, GST_SECOND, 25);
        sp->m_timestamp += GST_BUFFER_DURATION (buffer) ;

        // equals to gst_app_src_push_buffer (GST_APP_SRC_CAST (appsrc), buffer);
        g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
        gst_buffer_unmap(buffer, &map);
        gst_buffer_unref (buffer);

        if (ret != GST_FLOW_OK) {
            /* something wrong, stop pushing */
            LOG_ERROR_MSG ("push-buffer failed");
        }
    }

    // usleep (25 * 1000);

    return ret;
}

SrcPipeline::SrcPipeline (const SrcPipelineConfig& config)
{
    m_config = config;
    m_timestamp = 0;
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

    m_transCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.src_format.c_str(), "width", G_TYPE_INT, m_config.src_width,
          "height", G_TYPE_INT, m_config.src_height, NULL);
    // equals to gst_app_src_set_caps (GST_APP_SRC_CAST (m_appsrc), m_transCaps);
    g_object_set (G_OBJECT(m_appsrc), "caps", m_transCaps, NULL);
    gst_caps_unref (m_transCaps); 

    // equals to gst_app_src_set_stream_type (GST_APP_SRC_CAST (m_appsrc),
    //             GST_APP_STREAM_TYPE_STREAM);
    g_object_set (G_OBJECT(m_appsrc), "stream-type",
        GST_APP_STREAM_TYPE_STREAM, NULL);

    g_object_set (G_OBJECT(m_appsrc), "is-live", true, NULL);

    // full definition of appsrc callbacks
    /*
    GstAppSrcCallbacks callbacks = {cb_appsrc_need_data,
                            cb_appsrc_enough_data, cb_appsrc_seek_data};
    gst_app_src_set_callbacks (GST_APP_SRC_CAST (m_appsrc),
        &callbacks, reinterpret_cast<void*> (this), NULL);
    */
    g_signal_connect (m_appsrc, "need-data",
        G_CALLBACK (cb_appsrc_need_data), reinterpret_cast<void*> (this));

    gst_bin_add_many (GST_BIN (m_srcPipeline), m_appsrc, NULL);
    
    if (!(m_videoconv = gst_element_factory_make ("videoconvert", "videoconv"))) {
        LOG_ERROR_MSG ("Failed to create element videoconvert named videoconv");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_srcPipeline), m_videoconv, NULL);

    m_transCaps = gst_caps_new_simple ("video/x-raw", "format", G_TYPE_STRING,
        m_config.conv_format.c_str(), "width", G_TYPE_INT, m_config.conv_width,
          "height", G_TYPE_INT, m_config.conv_height, NULL);

    if (!(m_capfilter = gst_element_factory_make("capsfilter", "capfilter"))) {
        LOG_ERROR_MSG ("Failed to create element capsfilter named capfilter");
        goto exit;
    }

    g_object_set (G_OBJECT(m_capfilter), "caps", m_transCaps, NULL);
    gst_caps_unref (m_transCaps); 

    gst_bin_add_many (GST_BIN (m_srcPipeline), m_capfilter, NULL);

    if (!(m_display = gst_element_factory_make ("waylandsink", "display"))) {
        LOG_ERROR_MSG ("Failed to create element waylandsink named display");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_srcPipeline), m_display, NULL);

    if (!gst_element_link_many (m_appsrc, m_videoconv,
            m_capfilter,m_display, NULL)) {
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

void SrcPipeline::SetCallbacks (SrcGetDataFunc func, void* args)
{
    LOG_INFO_MSG ("src set callback called");

    m_getDataFunc = func;
    m_getDataArgs = args;
}
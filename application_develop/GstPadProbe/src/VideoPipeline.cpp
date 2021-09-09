/*
 * @Description: Implement of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:01:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-09-10 03:21:09
 */

#include "VideoPipeline.h"

static GstPadProbeReturn cb_sync_before_buffer_probe (
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    //LOG_INFO_MSG ("cb_sync_before_buffer_probe called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn cb_sync_buffer_probe (
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    //LOG_INFO_MSG ("cb_sync_buffer_probe called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        g_mutex_lock (&vp->m_syncMuxtex);
        g_atomic_int_inc (&vp->m_syncCount);
        g_cond_signal (&vp->m_syncCondition);
        g_mutex_unlock (&vp->m_syncMuxtex);
    }

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn cb_queue0_probe (
    GstPad* pad, 
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // LOG_INFO_MSG ("cb_queue0_probe called");

    VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER && !vp->isExited) {
        g_mutex_lock (&vp->m_syncMuxtex);
        while (g_atomic_int_get (&vp->m_syncCount) <= 0)
            g_cond_wait (&vp->m_syncCondition, &vp->m_syncMuxtex);
        if (!g_atomic_int_dec_and_test (&vp->m_syncCount)) {
            //LOG_INFO_MSG ("m_syncCount:%d/%d", vp->m_syncCount,
            //    vp->pipeline_id_);
        }
        g_mutex_unlock (&vp->m_syncMuxtex);
    }

    // osd the result
    if (vp->m_getResultFunc) {
        const std::shared_ptr<cv::Rect> result =
            vp->m_getResultFunc (vp->m_getResultArgs);
        if (result && vp->m_procDataFunc) {
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

#ifdef RTSP_SOURCE
static void cb_rtspsrc_pad_added (
    GstElement *src, GstPad *new_pad, gpointer user_data)
{
    GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;

	VideoPipeline* vp = reinterpret_cast<VideoPipeline*> (user_data);

    GstPad* sink_pad = gst_element_get_static_pad (
                    reinterpret_cast<GstElement*> (vp->m_rtph264depay), "sink");

	LOG_INFO_MSG ("Received new pad '%s' from '%s':", GST_PAD_NAME (new_pad),
        GST_ELEMENT_NAME (src));

	/* Check the new pad's name */
	if (!g_str_has_prefix (GST_PAD_NAME(new_pad), "recv_rtp_src_")) {
		LOG_ERROR_MSG ("It is not the right pad.  Need recv_rtp_src_. Ignoring.");
		goto exit;
	}

	/* If our converter is already linked, we have nothing to do here */
	if (gst_pad_is_linked(sink_pad)) {
		LOG_ERROR_MSG (" Sink pad from %s already linked. Ignoring.\n",
            GST_ELEMENT_NAME (src));
		goto exit;
	}

	/* Check the new pad's type */
	new_pad_caps = gst_pad_query_caps(new_pad, NULL);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	new_pad_type = gst_structure_get_name(new_pad_struct);

	/* Attempt the link */
	ret = gst_pad_link(new_pad, sink_pad);
	if (GST_PAD_LINK_FAILED (ret)) {
		LOG_ERROR_MSG ("Fail to link rtspsrc and rtph264depay");
	}

exit:
	/* Unreference the new pad's caps, if we got them */
	if (new_pad_caps != NULL)
		gst_caps_unref(new_pad_caps);

	/* Unreference the sink pad */
	gst_object_unref(sink_pad);
}
#endif

#ifdef FILE_SOURCE
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
        LOG_ERROR_MSG ("Fail to link qtdemux and h264parse");
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref (new_pad_caps);

    /* Unreference the sink pad */
    gst_object_unref (v_sinkpad);
}
#endif

VideoPipeline::VideoPipeline (const VideoPipelineConfig& config)
{
    m_config = config;
    m_syncCount = 0;
    isExited = false;
    m_queue0_probe = -1;
    m_trans_sink_probe = -1;
    m_trans_src_probe = -1;
    g_mutex_init (&m_syncMuxtex);
    g_cond_init  (&m_syncCondition);
    g_mutex_init (&m_mutex);
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

#ifdef RTSP_SOURCE
    if (!(m_source = gst_element_factory_make ("rtspsrc", "src"))) {
        LOG_ERROR_MSG ("Failed to create element rtspsrc named src");
        goto exit;
    }
    g_object_set (G_OBJECT (m_source), "location",
            m_config.src.c_str(), NULL);
    g_signal_connect(GST_OBJECT (m_source), "pad-added",
        G_CALLBACK(cb_rtspsrc_pad_added), reinterpret_cast<void*> (this));
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_source, NULL);

    if (!(m_rtph264depay = gst_element_factory_make ("rtph264depay", "depay"))) {
        LOG_ERROR_MSG ("Failed to create element rtph264depay named depay");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_rtph264depay, NULL);
#endif

#ifdef FILE_SOURCE
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
    // Link qtdemux with h264parse
    g_signal_connect (m_qtdemux, "pad-added",
        G_CALLBACK(cb_qtdemux_pad_added), reinterpret_cast<void*> (this));
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_qtdemux, NULL);

    if (!gst_element_link_many (m_source, m_qtdemux, NULL)) {
        LOG_ERROR_MSG ("Failed to link filesrc->qtdemux");
        goto exit;
    }
#endif

    if (!(m_h264parse = gst_element_factory_make ("h264parse", "parse"))) {
        LOG_ERROR_MSG ("Failed to create element h264parse named parse");
        goto exit;
    }
    gst_bin_add_many (GST_BIN (m_gstPipeline), m_h264parse, NULL);

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
                        GST_PAD_PROBE_TYPE_BUFFER), cb_queue0_probe, 
                        reinterpret_cast<void*> (this), NULL);
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

#ifdef RTSP_SOURCE
    if (!gst_element_link_many (m_rtph264depay, m_h264parse, m_decoder,
            m_tee, m_queue0, m_qtioverlay, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link rtph264depay->h264parse->qtivdec"
            "->tee->queue0->qtioverlay->waylandsink");
        goto exit;
    }
#endif

#ifdef FILE_SOURCE
    if (!gst_element_link_many (m_h264parse, m_decoder, m_tee, 
            m_queue0, m_qtioverlay, m_display, NULL)) {
        LOG_ERROR_MSG ("Failed to link h264parse->qtivdec"
            "->tee->queue0->qtioverlay->waylandsink");
        goto exit;
    }
#endif

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
    
    m_gstPad = gst_element_get_static_pad (m_qtivtrans, "sink");
    m_trans_sink_probe = gst_pad_add_probe (m_gstPad, (GstPadProbeType) (
                        GST_PAD_PROBE_TYPE_BUFFER), cb_sync_before_buffer_probe, 
                        reinterpret_cast<void*> (this), NULL);
    gst_object_unref (m_gstPad);

    m_gstPad = gst_element_get_static_pad (m_qtivtrans, "src");
    m_trans_src_probe = gst_pad_add_probe (m_gstPad, (GstPadProbeType) (
                        GST_PAD_PROBE_TYPE_BUFFER), cb_sync_buffer_probe, 
                        reinterpret_cast<void*> (this), NULL);
    gst_object_unref (m_gstPad);

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
        isExited = true;
        g_mutex_lock (&m_syncMuxtex);
        g_atomic_int_inc (&m_syncCount);
        g_cond_signal (&m_syncCondition);
        g_mutex_unlock (&m_syncMuxtex);

        gst_element_set_state (m_gstPipeline, GST_STATE_NULL);

        gst_object_unref (m_gstPipeline);

        m_gstPipeline = NULL;
    }

    if (m_trans_src_probe != -1 && m_queue0) {
        GstPad *gstpad = gst_element_get_static_pad (m_qtivtrans, "sink");
        if (!gstpad) {
            LOG_ERROR_MSG ("Could not find '%s' in '%s'", "src",
                GST_ELEMENT_NAME(m_qtivtrans));
        }
        gst_pad_remove_probe(gstpad, m_trans_src_probe);
        gst_object_unref (gstpad);
        m_trans_src_probe = -1;
    }

    if (m_trans_src_probe != -1 && m_queue0) {
        GstPad *gstpad = gst_element_get_static_pad (m_qtivtrans, "src");
        if (!gstpad) {
            LOG_ERROR_MSG ("Could not find '%s' in '%s'", "src",
                GST_ELEMENT_NAME(m_qtivtrans));
        }
        gst_pad_remove_probe(gstpad, m_trans_src_probe);
        gst_object_unref (gstpad);
        m_trans_src_probe = -1;
    }

    if (m_queue0_probe != -1 && m_queue0) {
        GstPad *gstpad = gst_element_get_static_pad (m_queue0, "src");
        if (!gstpad) {
            LOG_ERROR_MSG ("Could not find '%s' in '%s'", "src",
                GST_ELEMENT_NAME(m_queue0));
        }
        gst_pad_remove_probe(gstpad, m_queue0_probe);
        gst_object_unref (gstpad);
        m_queue0_probe = -1;
    }

    GstPad* teeSrcPad;
    while (teeSrcPad = gst_element_get_request_pad (m_tee, "src_%u")) {
        gst_element_release_request_pad (m_tee, teeSrcPad);
        g_object_unref (teeSrcPad);
    }

    g_mutex_clear (&m_mutex);
    g_mutex_clear (&m_syncMuxtex);
    g_cond_clear  (&m_syncCondition);
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
/*
 * @Description: Implement of VideoPipeline on DeepStream.
 * @version: 2.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-07-15 22:07:19
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2023-02-03 09:49:52
 */

#include "VideoPipeline.h"

static GstPadProbeReturn cb_sync_before_buffer_probe(
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // LOG_INFO("cb_sync_before_buffer_probe called");

    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn cb_sync_after_buffer_probe(
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // LOG_INFO("cb_sync_after_buffer_probe called");

    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        g_mutex_lock(&vp->m_syncMuxtex);
        g_atomic_int_inc(&vp->m_syncCount);
        g_cond_signal(&vp->m_syncCondition);
        g_mutex_unlock(&vp->m_syncMuxtex);
    }

    return GST_PAD_PROBE_OK;
}

static GstPadProbeReturn cb_queue0_probe(
    GstPad* pad, 
    GstPadProbeInfo* info,
    gpointer user_data)
{
    // LOG_INFO("cb_queue0_probe called");

    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);
    GstBuffer* buffer = (GstBuffer*) info->data;

    // sync
    // if (info->type & GST_PAD_PROBE_TYPE_BUFFER && !vp->m_isExited) {
    //     g_mutex_lock(&vp->m_syncMuxtex);
    //     while(g_atomic_int_get(&vp->m_syncCount) <= 0)
    //         g_cond_wait(&vp->m_syncCondition, &vp->m_syncMuxtex);
    //     if (!g_atomic_int_dec_and_test(&vp->m_syncCount)) {
    //         //LOG_INFO("m_syncCount:{}/{}", vp->m_syncCount,
    //         //    pipeline_id);
    //     }
    //     g_mutex_unlock(&vp->m_syncMuxtex);
    // }

    // osd the result
    if (vp->m_getResultFunc) {
        const std::shared_ptr<std::vector<OSDObject> > results =
            vp->m_getResultFunc(vp->m_getResultArgs);
        // to-do: construct nvdsosd metadata
        if (vp->m_procResultFunc) {
            vp->m_procResultFunc(buffer, results);
        }
    }

    // LOG_INFO("cb_queue0_probe exited");

    return GST_PAD_PROBE_OK;
}

static GstFlowReturn cb_appsink_new_sample(
    GstElement* appsink,
    gpointer user_data)
{
    // LOG_INFO("cb_appsink_new_sample called");

    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);
    GstSample* sample = nullptr;

    if (!vp->m_dump) {
        GST_DEBUG_BIN_TO_DOT_FILE(GST_BIN(vp->m_pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "video-pipeline");
        vp->m_dump = true;
    }

    g_signal_emit_by_name(appsink, "pull-sample", &sample);
    if (!sample) {
        return GST_FLOW_OK;
    }

    if (vp->m_putFrameFunc) {
        vp->m_putFrameFunc(sample, vp->m_putFrameArgs);
    } else {
        gst_sample_unref(sample);
    }

    return GST_FLOW_OK;
}

static gboolean cb_seek_decoded_file(gpointer user_data)
{
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);

    LOG_INFO("============================================");
    LOG_INFO("cb_seek_decoded_file called({})", pipeline_id);
    LOG_INFO("============================================");

    gst_element_set_state(vp->m_pipeline, GST_STATE_PAUSED);

    if (!gst_element_seek(vp->m_pipeline, 1.0, GST_FORMAT_TIME,
        (GstSeekFlags)(GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_FLUSH),
        GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        LOG_WARN("Failed to seed the source file in pipeline");
    }

    gst_element_set_state(vp->m_pipeline, GST_STATE_PLAYING);

    return false;
}

static GstPadProbeReturn cb_reset_stream_probe(
    GstPad* pad,
    GstPadProbeInfo* info,
    gpointer user_data)
{
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);

    if (info->type & GST_PAD_PROBE_TYPE_BUFFER) {
        GST_BUFFER_PTS(GST_BUFFER(info->data)) += vp->m_prev_accumulated_base;
    }

    if (info->type & GST_PAD_PROBE_TYPE_EVENT_BOTH) {
        GstEvent* event = GST_EVENT(info->data);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            GstSegment *segment;
            gst_event_parse_segment(event, (const GstSegment **) &segment);
            segment->base = vp->m_accumulated_base;
            vp->m_prev_accumulated_base = vp->m_accumulated_base;
            vp->m_accumulated_base += segment->stop;
        } else if (GST_EVENT_TYPE(event) == GST_EVENT_EOS) {
            g_timeout_add(1, cb_seek_decoded_file, vp);
        }

        switch(GST_EVENT_TYPE(event)) {
            case GST_EVENT_EOS:
            case GST_EVENT_QOS:
            case GST_EVENT_SEGMENT:
            case GST_EVENT_FLUSH_START:
            case GST_EVENT_FLUSH_STOP:
                return GST_PAD_PROBE_DROP;
            default:
                break;
        }
    }

    return GST_PAD_PROBE_OK;
}

static void cb_decodebin_child_added(GstChildProxy* child_proxy, GObject* object,
    gchar* name, gpointer user_data)
{
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);

    LOG_INFO("cb_decodebin_child_added called({},'{}' added)", pipeline_id, name);

    if (g_strrstr(name, "nvv4l2decoder") == name) {
        g_object_set(object, "cudadec-memtype", 2, nullptr);

        if (g_strstr_len(vp->m_config.uri.c_str(), -1, "file:/") ==
            vp->m_config.uri.c_str() && vp->m_config.file_loop) {
            GstPad* gst_pad = gst_element_get_static_pad(GST_ELEMENT(object), "sink");
            vp->m_dec_sink_probe = gst_pad_add_probe(gst_pad, (GstPadProbeType)(
                GST_PAD_PROBE_TYPE_EVENT_BOTH | GST_PAD_PROBE_TYPE_EVENT_FLUSH |
                GST_PAD_PROBE_TYPE_BUFFER), cb_reset_stream_probe, static_cast<void*>(vp), nullptr);
            gst_object_unref(gst_pad);

            vp->m_decoder = GST_ELEMENT(object);
            gst_object_ref(object);
        } else if (g_strstr_len(vp->m_config.uri.c_str(), -1, "rtsp:/") ==
            vp->m_config.uri.c_str()) {
            vp->m_decoder = GST_ELEMENT(object);
            gst_object_ref(object);
        }
    } else if ((g_strrstr(name, "h264parse") == name) ||
            (g_strrstr(name, "h265parse") == name)) {
        LOG_INFO("set config-interval of {} to {}", name, -1);
        g_object_set(object, "config-interval", -1, nullptr);
    }

done:
    return;
}

static void cb_uridecodebin_source_setup(GstElement* object, GstElement* source,
    gpointer user_data)
{
    LOG_INFO("cb_uridecodebin_source_setup called");
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "latency")) {
        LOG_INFO("cb_uridecodebin_source_setup set {} latency", vp->m_config.rtsp_latency);
        g_object_set(G_OBJECT(source), "latency", vp->m_config.rtsp_latency, nullptr);
    }

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(source), "protocols")) {
        LOG_INFO("set protocols of source to {}", vp->m_config.rtp_protocol);
        g_object_set(G_OBJECT(source), "protocols", vp->m_config.rtp_protocol, nullptr);
    }
}

static void cb_uridecodebin_pad_added(GstElement* decodebin, GstPad* pad,
    gpointer user_data)
{
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);
    GstPad* sinkpad = nullptr;

    GstCaps* caps = gst_pad_query_caps(pad, nullptr);
    const GstStructure* str = gst_caps_get_structure(caps, 0);
    const gchar* name = gst_structure_get_name(str);
    
    LOG_INFO("cb_uridecodebin_pad_added called {}", name);
    LOG_INFO("structure:{}", gst_structure_to_string(str));

    if (g_str_has_prefix (name, "video/x-raw")) {
        if (vp->m_config.enable_display || vp->m_config.enable_appsink) {
            sinkpad = gst_element_get_static_pad(vp->m_tee0, "sink");
        } else {
            sinkpad = gst_element_get_static_pad(vp->m_fakesink, "sink");
        }

        if (sinkpad && gst_pad_link(pad, sinkpad) == GST_PAD_LINK_OK) {
            LOG_INFO("Success to link uridecodebin into pipeline");
        } else {
            LOG_ERROR("Failed to link uridecodebin to pipeline");
        }

        if (sinkpad) {
            gst_object_unref(sinkpad);
        }
    }

    gst_caps_unref(caps);
}

static void cb_uridecodebin_child_added(GstChildProxy* child_proxy,
    GObject* object, gchar* name, gpointer user_data)
{
    VideoPipeline* vp = static_cast<VideoPipeline*>(user_data);

    LOG_INFO("cb_uridecodebin_child_added called({},'{}' added)", pipeline_id, name);

    if (g_strrstr(name, "decodebin") == name) {
        g_signal_connect(G_OBJECT(object), "child-added",
            G_CALLBACK(cb_decodebin_child_added), vp);
    }

done:
    return;
}

VideoPipeline::VideoPipeline(const VideoPipelineConfig& config)
{
    m_config = config;
    m_syncCount = 0;
    m_isExited = false;
    m_queue00_src_probe = -1;
    m_cvt_sink_probe = -1;
    m_cvt_src_probe = -1;
    m_dec_sink_probe = -1;
    m_prev_accumulated_base = 0;
    m_accumulated_base = 0;
    m_dump = false;

    m_putFrameFunc = nullptr;
    m_putFrameArgs = nullptr;
    m_getResultFunc = nullptr;
    m_getResultArgs = nullptr;
    m_procResultFunc = nullptr;

    g_mutex_init(&m_syncMuxtex);
    g_cond_init(&m_syncCondition);
    g_mutex_init(&m_mutex);
}

VideoPipeline::~VideoPipeline()
{
    Destroy();
}

GstElement* VideoPipeline::CreateUridecodebin()
{
    if (!(m_source = gst_element_factory_make("uridecodebin", "uridecodebin0"))) {
        LOG_ERROR("Failed to create element uridecodebin named uridecodebin0");
        return nullptr;
    }

    g_object_set (G_OBJECT(m_source), "uri", m_config.uri.c_str(), nullptr);
    LOG_INFO("Set uri of uridecodebin to {}", m_config.uri);

    g_signal_connect(G_OBJECT(m_source), "source-setup", G_CALLBACK(
        cb_uridecodebin_source_setup), this);
    g_signal_connect(G_OBJECT(m_source), "pad-added",    G_CALLBACK(
        cb_uridecodebin_pad_added),    this);
    g_signal_connect(G_OBJECT(m_source), "child-added",  G_CALLBACK(
        cb_uridecodebin_child_added),  this);

    gst_bin_add_many(GST_BIN(m_pipeline), m_source, nullptr);

    return m_source;
}

GstElement* VideoPipeline::CreateV4l2src()
{
    if (!(m_source = gst_element_factory_make("v4l2src", "v4l2src0"))) {
        LOG_ERROR("Failed to create element v4l2src named v4l2src0");
        return nullptr;
    }
    g_object_set (G_OBJECT (m_source), "device", m_config.src_device.c_str(), nullptr);
    gst_bin_add_many (GST_BIN (m_pipeline), m_source, nullptr);

    GstCaps* caps = gst_caps_new_simple ("image/jpeg",
            "width", G_TYPE_INT, m_config.src_width,
            "height", G_TYPE_INT, m_config.src_height,
            "framerate", GST_TYPE_FRACTION, m_config.framerate_n, m_config.framerate_d,
            "format", G_TYPE_STRING, m_config.src_format.c_str(), nullptr);

    if (!(m_capfilter0 = gst_element_factory_make ("capsfilter", "capfilter0"))) {
        TS_ERR_MSG_V ("Failed to create element capsfilter named capfilter0");
        return nullptr;
    }

    g_object_set(G_OBJECT(m_capfilter0), "caps", caps, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many (GST_BIN (m_pipeline), m_capfilter0, nullptr);

    if (!(m_decoder = gst_element_factory_make("nvjpegdec", "nvjpegdec0"))) {
        LOG_ERROR("Failed to create element nvjpegdec named nvjpegdec0");
        return nullptr;
    }
    gst_bin_add_many(GST_BIN(m_pipeline), m_decoder, nullptr);

    if (!gst_element_link_many(m_source, m_capfilter0, m_decoder, nullptr)) {
        LOG_ERROR("Failed to link v4l2src0->capfilter0->nvjpegdec0");
        return nullptr;
    }

    return m_decoder;
}

bool VideoPipeline::Create()
{
    GstCaps* cvt_caps;
    GstPad* gst_pad;
    GstCapsFeatures* feature;
    GstElement* input;

    if (!(m_pipeline = gst_pipeline_new("video-pipeline"))) {
        LOG_ERROR("Failed to create pipeline named video-pipeline");
        goto exit;
    }
    gst_pipeline_set_auto_flush_bus(GST_PIPELINE(m_pipeline), true);

    input = m_config.input_type == VideoType::USB_CAMERE ? CreateV4l2src() : CreateUridecodebin();
    if (!input) {
        LOG_ERROR("Can't process input source.");
        goto exit;
    }

    if (m_config.input_type == VideoType::USB_CAMERE) {
        if (!gst_element_link_many(input, m_tee0, nullptr)) {
            LOG_ERROR("Failed to link nvjpegdec0->tee0");
            goto exit;
        }
    }

    if (!(m_tee0 = gst_element_factory_make("tee", "tee0"))) {
        LOG_ERROR("Failed to create element tee0 named tee0");
        goto exit;
    }
    gst_bin_add_many(GST_BIN(m_pipeline), m_tee0, nullptr);

    if (!(m_queue00 = gst_element_factory_make("queue", "queue0"))) {
        LOG_ERROR("Failed to create element queue named queue0");
        goto exit;
    }

    // add probe to queue0
    gst_pad = gst_element_get_static_pad(m_queue00, "src");
    m_queue00_src_probe = gst_pad_add_probe(gst_pad, (GstPadProbeType)(
                        GST_PAD_PROBE_TYPE_BUFFER), cb_queue0_probe, 
                        static_cast<void*>(this), nullptr);
    gst_object_unref(gst_pad);

    gst_bin_add_many(GST_BIN(m_pipeline), m_queue00, nullptr);

    if (!gst_element_link_many(m_tee0, m_queue00, nullptr)) {
        LOG_ERROR("Failed to link tee0->queue00");
        goto exit;
    }

    if (!m_config.enable_hdmi || !m_config.enable_rtmp) {
        if (!(m_fakesink = gst_element_factory_make("fakesink", "fakesink0"))) {
            LOG_ERROR("Failed to create element fakesink named fakesink0");
            goto exit;
        }
        g_object_set(G_OBJECT(m_fakesink), "sync", true, nullptr);

        gst_bin_add_many(GST_BIN(m_pipeline), m_fakesink, nullptr);

        if (!gst_element_link_many(m_queue00, m_fakesink, nullptr)) {
            LOG_ERROR("Failed to link queue00->fakesink0");
            goto exit;
        }
    } else {
        if (!(m_tee1 = gst_element_factory_make("tee", "tee1"))) {
        LOG_ERROR("Failed to create element tee0 named tee1");
        goto exit;
        }
        gst_bin_add_many(GST_BIN(m_pipeline), m_tee1, nullptr);

        if (!gst_element_link_many(m_queue00, m_tee1, nullptr)) {
            LOG_ERROR("Failed to link queue00->tee1");
            goto exit;
        }

        if (m_config.enable_hdmi) {
            if (!(m_queue10 = gst_element_factory_make("queue", "queue10"))) {
                LOG_ERROR("Failed to create element queue named queue10");
                goto exit;
            }
            gst_bin_add_many(GST_BIN(m_pipeline), m_queue10, nullptr);

            if (!(m_nveglglessink = gst_element_factory_make("nveglglessink", "nveglglessink0"))) {
                LOG_ERROR("Failed to create element nveglglessink named nveglglessink0");
                goto exit;
            }
            g_object_set(G_OBJECT(m_nveglglessink),
                "sync", m_config.sync,
                "window-x", m_config.window_x,
                "window-y", m_config.window_y,
                "window-width", m_config.window_width,
                "window-height", m_config.window_height, nullptr);

            gst_bin_add_many(GST_BIN(m_pipeline), m_nveglglessink, nullptr);

            if (!gst_element_link_many(m_tee1, m_queue10, m_nveglglessink, nullptr)) {
                LOG_ERROR("Failed to link tee1->queue10->nveglglessink0");
                goto exit;
            }
        }

        if (m_config.enable_rtmp) {
            if (!(m_queue11 = gst_element_factory_make("queue", "queue11"))) {
                LOG_ERROR("Failed to create element queue named queue11");
                goto exit;
            }
            gst_bin_add_many(GST_BIN(m_pipeline), m_queue11, nullptr);

            if (!(m_nvvideoconvert0 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert0"))) {
                LOG_ERROR("Failed to create element nvvideoconvert named nvvideoconvert0");
                goto exit;
            }
            g_object_set(G_OBJECT(m_nvvideoconvert0), "nvbuf-memory-type", m_config.cvt_memory_type, nullptr);
            gst_bin_add_many(GST_BIN(m_pipeline), m_nvvideoconvert0, nullptr);

            cvt_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12", nullptr);
            feature = gst_caps_features_new("memory:NVMM", nullptr);
            gst_caps_set_features(cvt_caps, 0, feature);

            if (!(m_capfilter1 = gst_element_factory_make("capsfilter", "capfilter0"))) {
                LOG_ERROR("Failed to create element capsfilter named capfilter0");
                goto exit;
            }

            g_object_set(G_OBJECT(m_capfilter1), "caps", cvt_caps, nullptr);
            gst_caps_unref(cvt_caps);

            gst_bin_add_many(GST_BIN(m_pipeline), m_capfilter1, nullptr);

            if (!(m_encoder = gst_element_factory_make("nvv4l2h264enc", "nvv4l2h264enc0"))) {
                LOG_ERROR("Failed to create element nvv4l2h264enc named nvv4l2h264enc0");
                goto exit;
            }
            g_object_set(G_OBJECT(m_encoder), "bitrate", m_config.enc_bitrate,
                "iframeinterval", m_config.enc_iframe_interval, nullptr);
            gst_bin_add_many(GST_BIN(m_pipeline), m_encoder, nullptr);

            if (!(m_flvmux = gst_element_factory_make("flvmux", "flvmux0"))) {
                LOG_ERROR("Failed to create element flvmux named flvmux0");
                goto exit;
            }
            gst_bin_add_many(GST_BIN(m_pipeline), m_flvmux, nullptr);

            if (!(m_rtmpsink = gst_element_factory_make("rtmpsink", "rtmpsink"))) {
                LOG_ERROR("Failed to create element rtmpsink named rtmpsink0");
                goto exit;
            }
            g_object_set(G_OBJECT(m_encoder), "location", m_config.rtmp_url.c_str(), nullptr);
            gst_bin_add_many(GST_BIN(m_pipeline), m_rtmpsink, nullptr);

            if (!gst_element_link_many(m_tee1, m_queue11, m_nvvideoconvert0,
                m_capfilter1, m_encoder, m_flvmux, m_rtmpsink, nullptr)) {
                LOG_ERROR("Failed to link tee1->queue11->nvvideoconvert0->capfilter1->nvv4l2h264enc0->flvmux0->rtmpsink0");
                goto exit;
            }
        }
    }

    if (m_config.enable_appsink) {
        if (!(m_queue01 = gst_element_factory_make("queue", "queue1"))) {
            LOG_ERROR("Failed to create element queue named queue1");
            goto exit;
        }
        gst_bin_add_many(GST_BIN(m_pipeline), m_queue01, nullptr);

        if (!(m_nvvideoconvert1 = gst_element_factory_make("nvvideoconvert", "nvvideoconvert1"))) {
            LOG_ERROR("Failed to create element nvvideoconvert named nvvideoconvert1");
            goto exit;
        }

        g_object_set(G_OBJECT(m_nvvideoconvert1), "nvbuf-memory-type", m_config.cvt_memory_type, nullptr);

        gst_bin_add_many(GST_BIN(m_pipeline), m_nvvideoconvert1, nullptr);

        cvt_caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, m_config.cvt_format.c_str(), nullptr);
        feature = gst_caps_features_new("memory:NVMM", nullptr);
        gst_caps_set_features(cvt_caps, 0, feature);

        if (!(m_capfilter2 = gst_element_factory_make("capsfilter", "capfilter2"))) {
            LOG_ERROR("Failed to create element capsfilter named capfilter2");
            goto exit;
        }

        g_object_set(G_OBJECT(m_capfilter2), "caps", cvt_caps, nullptr);
        gst_caps_unref(cvt_caps);

        gst_bin_add_many(GST_BIN(m_pipeline), m_capfilter2, nullptr);

        // gst_pad = gst_element_get_static_pad(m_nvvideoconvert1, "sink");
        // m_cvt_sink_probe = gst_pad_add_probe(gst_pad, (GstPadProbeType)(
        //                     GST_PAD_PROBE_TYPE_BUFFER), cb_sync_before_buffer_probe,
        //                     static_cast<void*>(this), nullptr);
        // gst_object_unref(gst_pad);

        // gst_pad = gst_element_get_static_pad(m_nvvideoconvert1, "src");
        // m_cvt_sink_probe = gst_pad_add_probe(gst_pad, (GstPadProbeType)(
        //                     GST_PAD_PROBE_TYPE_BUFFER), cb_sync_after_buffer_probe,
        //                     static_cast<void*>(this), nullptr);
        // gst_object_unref(gst_pad);

        if (!(m_appsink = gst_element_factory_make("appsink", "appsink"))) {
            LOG_ERROR("Failed to create element appsink named appsink");
            goto exit;
        }

        g_object_set(m_appsink, "emit-signals", true, nullptr);

        g_signal_connect(m_appsink, "new-sample",
            G_CALLBACK(cb_appsink_new_sample), static_cast<void*>(this));

        gst_bin_add_many(GST_BIN(m_pipeline), m_appsink, nullptr);

        if (!gst_element_link_many(m_tee0, m_queue01, m_nvvideoconvert1, m_capfilter2, m_appsink, nullptr)) {
            LOG_ERROR("Failed to link tee0->queue01->nvvideoconvert1->capfilter1->appsink");
            goto exit;
        }
    }

    return true;

exit:
    LOG_ERROR("Failed to create video pipeline");
    return false;
}

bool VideoPipeline::Start(void)
{
    LOG_INFO("Start pipeline called");

    if (GST_STATE_CHANGE_FAILURE == gst_element_set_state(m_pipeline,
        GST_STATE_PLAYING)) {
        LOG_ERROR("Failed to set pipeline to playing state");
        return false;
    }

    return true;
}

bool VideoPipeline::Pause(void)
{
    GstState state, pending;

    LOG_INFO("Stop Pipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state(
        m_pipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PAUSED) {
        return true;
    } else if (state == GST_STATE_PLAYING) {
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        gst_element_get_state(m_pipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN("Invalid state of pipeline {}",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

bool VideoPipeline::Resume(void)
{
    GstState state, pending;

    LOG_INFO("Restart Pipeline called");

    if (GST_STATE_CHANGE_ASYNC == gst_element_get_state(
        m_pipeline, &state, &pending, 5 * GST_SECOND / 1000)) {
        LOG_WARN("Failed to get state of pipeline");
        return false;
    }

    if (state == GST_STATE_PLAYING) {
        return true;
    } else if (state == GST_STATE_PAUSED) {
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        gst_element_get_state(m_pipeline, &state, &pending,
            GST_CLOCK_TIME_NONE);
        return true;
    } else {
        LOG_WARN("Invalid state of pipeline{}",
            GST_STATE_CHANGE_ASYNC);
        return false;
    }
}

void VideoPipeline::Destroy(void)
{
    GstPad* teeSrcPad;
    while(teeSrcPad = gst_element_get_request_pad(m_tee0, "src_%u")) {
        gst_element_release_request_pad(m_tee0, teeSrcPad);
        g_object_unref(teeSrcPad);
    }

    while(teeSrcPad = gst_element_get_request_pad(m_tee1, "src_%u")) {
        gst_element_release_request_pad(m_tee1, teeSrcPad);
        g_object_unref(teeSrcPad);
    }

    if (m_pipeline) {
        m_isExited = true;
        g_mutex_lock(&m_syncMuxtex);
        g_atomic_int_inc(&m_syncCount);
        g_cond_signal(&m_syncCondition);
        g_mutex_unlock(&m_syncMuxtex);

        gst_element_set_state(m_pipeline, GST_STATE_NULL);

        gst_object_unref(m_pipeline);

        m_pipeline = nullptr;
    }

    // if (m_cvt_sink_probe != -1 && m_nvvideoconvert1) {
    //     GstPad *gstpad = gst_element_get_static_pad(m_nvvideoconvert1, "sink");
    //     if (!gstpad) {
    //         LOG_ERROR("Could not find '{}' in '{}'", "sink", GST_ELEMENT_NAME(m_nvvideoconvert1));
    //     }
    //     gst_pad_remove_probe(gstpad, m_cvt_sink_probe);
    //     gst_object_unref(gstpad);
    //     m_cvt_sink_probe = -1;
    // }

    // if (m_cvt_src_probe != -1 && m_nvvideoconvert1) {
    //     GstPad *gstpad = gst_element_get_static_pad(m_nvvideoconvert1, "src");
    //     if (!gstpad) {
    //         LOG_ERROR("Could not find '{}' in '{}'", "src", GST_ELEMENT_NAME(m_nvvideoconvert1));
    //     }
    //     gst_pad_remove_probe(gstpad, m_cvt_src_probe);
    //     gst_object_unref(gstpad);
    //     m_cvt_src_probe = -1;
    // }

    if (m_dec_sink_probe != -1 && m_decoder) {
        GstPad *gstpad = gst_element_get_static_pad(m_decoder, "sink");
        if (!gstpad) {
            LOG_ERROR("Could not find '{}' in '{}'", "sink", GST_ELEMENT_NAME(m_decoder));
        }
        gst_pad_remove_probe(gstpad, m_dec_sink_probe);
        gst_object_unref(gstpad);
        m_dec_sink_probe = -1;
    }

    if (m_queue00_src_probe != -1 && m_queue00) {
        GstPad *gstpad = gst_element_get_static_pad(m_queue00, "src");
        if (!gstpad) {
            LOG_ERROR("Could not find '{}' in '{}'", "src", GST_ELEMENT_NAME(m_queue00));
        }
        gst_pad_remove_probe(gstpad, m_queue00_src_probe);
        gst_object_unref(gstpad);
        m_queue00_src_probe = -1;
    }

    g_mutex_clear(&m_mutex);
    g_mutex_clear(&m_syncMuxtex);
    g_cond_clear(&m_syncCondition);
}

void VideoPipeline::SetCallbacks(PutFrameFunc func, void* args)
{
    LOG_INFO("set PutFrameFunc callback called");

    m_putFrameFunc = func;
    m_putFrameArgs = args;
}

void VideoPipeline::SetCallbacks(GetResultFunc func, void* args)
{
    LOG_INFO("set GetResultFunc callback called");

    m_getResultFunc = func;
    m_getResultArgs = args;
}

void VideoPipeline::SetCallbacks(ProcResultFunc func)
{
    LOG_INFO("set ProcResultFunc callback called");

    m_procResultFunc = func;
}

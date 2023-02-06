/*
 * @Description: Test program of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-07-15 22:07:33
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2023-02-06 20:45:00
 */

#include <sys/stat.h>
#include <iostream>
#include <sstream>
#include <fstream>

#include <jsoncpp/json/json.h>
#include <gflags/gflags.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>

#include "Common.h"
#include "VideoPipeline.h"
#include "DoubleBufferCache.h"

static GMainLoop* g_main_loop = NULL;
Json::Reader g_reader;

static void Parse(VideoPipelineConfig& config, std::string& config_path)
{
    Json::Value root;
    std::ifstream in(config_path, std::ios::binary);
    g_reader.parse(in, root);

    if (root.isMember("name")) {
        config.pipeline_id = root["name"].asString();
        LOG_INFO("New pieline name: {}", config.pipeline_id);
    }

    if (root.isMember("input-config")) {
        Json::Value inputConfig = root["input-config"];
        config.input_type = inputConfig["type"].asInt();    // 0-MP4 / 1-RTSP / 2-USB Camera
        LOG_INFO("Pipeline[{}]: type: {}", config.pipeline_id, config.input_type);

        config.src_uri = inputConfig["stream"]["uri"].asString();
        LOG_INFO("Pipeline[{}]: input: {}", config.pipeline_id, config.src_uri);
        config.file_loop = inputConfig["stream"]["file-loop"].asBool();
        LOG_INFO("Pipeline[{}]: file-loop: {}", config.pipeline_id, config.file_loop);
        config.rtsp_latency = inputConfig["stream"]["rtsp-latency"].asInt();
        LOG_INFO("Pipeline[{}]: rtsp-latency: {}", config.pipeline_id, config.rtsp_latency);
        config.rtp_protocol = inputConfig["stream"]["rtp-protocol"].asInt();
        LOG_INFO("Pipeline[{}]: rtp-protocol: {}", config.pipeline_id, config.rtp_protocol);
        config.src_device = inputConfig["usb-camera"]["device"].asString();
        LOG_INFO("Pipeline[{}]: usb camera device: {}", config.pipeline_id, config.src_device);
        config.src_format = inputConfig["usb-camera"]["format"].asString();
        LOG_INFO("Pipeline[{}]: usb camera output format: {}", config.pipeline_id, config.src_format);
        config.src_width = inputConfig["usb-camera"]["width"].asInt();
        LOG_INFO("Pipeline[{}]: usb camera output width: {}", config.pipeline_id, config.src_width);
        config.src_height = inputConfig["usb-camera"]["height"].asInt();
        LOG_INFO("Pipeline[{}]: usb camera output height: {}", config.pipeline_id, config.src_height);
        config.src_framerate_n = inputConfig["usb-camera"]["framerate-n"].asInt();
        LOG_INFO("Pipeline[{}]: usb camera output height: {}", config.pipeline_id, config.src_framerate_n);
        config.src_framerate_d = inputConfig["usb-camera"]["framerate-d"].asInt();
        LOG_INFO("Pipeline[{}]: usb camera output height: {}", config.pipeline_id, config.src_framerate_d);
    }

    if (root.isMember("output-config")) {
        Json::Value outputConfig = root["output-config"];
        if (outputConfig.isMember("display")) {
            Json::Value displayConfig = outputConfig["display"];
            config.enable_hdmi = displayConfig["enable"].asBool();
            LOG_INFO("Pipeline[{}]: enable-hdmi: {}", config.pipeline_id, config.enable_hdmi);
            config.hdmi_sync = displayConfig["sync"].asBool();
            LOG_INFO("Pipeline[{}]: hdmi-sync: {}", config.pipeline_id, config.hdmi_sync);
            config.window_x = displayConfig["left"].asInt();
            LOG_INFO("Pipeline[{}]: window-x: {}", config.pipeline_id, config.window_x);
            config.window_y = displayConfig["top"].asInt();
            LOG_INFO("Pipeline[{}]: window-y: {}", config.pipeline_id, config.window_y);
            config.window_width = displayConfig["width"].asInt();
            LOG_INFO("Pipeline[{}]: window-width: {}", config.pipeline_id, config.window_width);
            config.window_height = displayConfig["height"].asInt();
            LOG_INFO("Pipeline[{}]: window-height: {}", config.pipeline_id, config.window_height);
        }

        if (outputConfig.isMember("rtmp")) {
            Json::Value rtmpConfig = outputConfig["rtmp"];
            config.enable_rtmp = rtmpConfig["enable"].asBool();
            LOG_INFO("Pipeline[{}]: enable-rtmp: {}", config.pipeline_id, config.enable_rtmp);
            config.enc_bitrate = rtmpConfig["bitrate"].asInt();
            LOG_INFO("Pipeline[{}]: encode-birtate: {}", config.pipeline_id, config.enc_bitrate);
            config.enc_iframe_interval = rtmpConfig["iframeinterval"].asInt();
            LOG_INFO("Pipeline[{}]: encode-iframeinterval: {}", config.pipeline_id, config.enc_iframe_interval);
            config.rtmp_uri = rtmpConfig["uri"].asString();
            LOG_INFO("Pipeline[{}]: rtmp-uri: {}", config.pipeline_id, config.rtmp_uri);
        }

        if (outputConfig.isMember("inference")) {
            Json::Value inferenceConfig = outputConfig["inference"];
            config.enable_appsink = inferenceConfig["enable"].asBool();
            LOG_INFO("Pipeline[{}]: enable-appsink: {}", config.pipeline_id, config.enable_appsink);
            config.cvt_memory_type = inferenceConfig["memory-type"].asInt();
            LOG_INFO("Pipeline[{}]: videoconvert memory type: {}", config.pipeline_id, config.cvt_memory_type);
            config.cvt_format = inferenceConfig["format"].asString();
            LOG_INFO("Pipeline[{}]: videoconvert format: {}", config.pipeline_id, config.cvt_format);
        }
    }
}

static bool validateConfigPath(const char* name, const std::string& value) 
{ 
    if (0 == value.compare ("")) {
        LOG_ERROR("You must specify a config file!");
        return false;
    }

    struct stat statbuf;
    if (0 == stat(value.c_str(), &statbuf)) {
        return true;
    }

    LOG_ERROR("Can't stat model file: {}", value);
    return false;
}

DEFINE_string(config_path, "./pipeline.json", "Model config file path.");
DEFINE_validator(config_path, &validateConfigPath);

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags(&argc, &argv, true);

    VideoPipelineConfig m_vpConfig;
    VideoPipeline *m_vp;

    Parse(m_vpConfig, FLAGS_config_path);

    gst_init(&argc, &argv);

    g_setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/ricardo/workSpace/gstreamer-example/ai_integration/deepstream/build", true);

    if (!(g_main_loop = g_main_loop_new(NULL, FALSE))) {
        LOG_ERROR("Failed to new a object with type GMainLoop");
        goto exit;
    }

    m_vp = new VideoPipeline(m_vpConfig);

    if (!m_vp->Create()) {
        LOG_ERROR("Pipeline Create failed: lack of elements");
        goto exit;
    }

    m_vp->Start();

    g_main_loop_run(g_main_loop);

exit:
    if (g_main_loop) g_main_loop_unref(g_main_loop);

    if (m_vp) {
        // m_vp->Destroy();
        delete m_vp;
        m_vp = NULL;
    }

    google::ShutDownCommandLineFlags();
    return 0;
}
/*
 * @Description: Test program of VideoPipeline.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-07-15 22:07:33
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2023-02-03 06:02:14
 */

#include <sys/stat.h>
#include <iostream>
#include <sstream>

#include <json-glib/json-glib.h>
#include <gflags/gflags.h>
#include <gstnvdsmeta.h>
#include <nvbufsurface.h>

#include "Common.h"
#include "VideoPipeline.h"
#include "DoubleBufferCache.h"

static GMainLoop* g_main_loop = NULL;

void ProcResult(GstBuffer* buffer, const std::shared_ptr<std::vector<OSDObject> >& results)
{
    NvDsObjectMeta *obj_meta = NULL;
    NvDsMetaList* l_frame = NULL;
    NvDsMetaList* l_obj = NULL;
    NvDsDisplayMeta* display_meta = NULL;

	// 遍历GstBuffer取出NvDsBatchMeta
    NvDsBatchMeta* batch_meta = gst_buffer_get_nvds_batch_meta(buffer);

	// 遍历batch中的所有帧
    // 假如只有一路流(即nvstreammux的batch-size=1)，那么frame_meta_list长度为1
    for (l_frame = batch_meta->frame_meta_list; l_frame != NULL;
      l_frame = l_frame->next) {
        NvDsFrameMeta *frame_meta =(NvDsFrameMeta*)(l_frame->data);
        int offset = 0;

        for (size_t i = 0; i < results->size(); i++) {
            // 添加自定义的OSD信息，具体有哪些设置参数可以参考官方API文档
            display_meta = nvds_acquire_display_meta_from_pool(batch_meta);
            NvOSD_RectParams *rect_params = &display_meta->rect_params[i];
            
            rect_params->left = (*results)[i].x;
            rect_params->top = (*results)[i].y;
            rect_params->width = (*results)[i].width;
            rect_params->height = (*results)[i].height;

            rect_params->border_color.red = (*results)[i].r;
            rect_params->border_color.green = (*results)[i].g;
            rect_params->border_color.blue = (*results)[i].b;
            rect_params->border_color.alpha = (*results)[i].a;

            nvds_add_display_meta_to_frame(frame_meta, display_meta);
        }
    }
}

static bool Parse(VideoPipelineConfig& config, std::string& config_path)
{
    JsonParser* parser = NULL;
    JsonNode*   root   = NULL;
    JsonObject* object = NULL;
    GError*     error  = NULL;
    bool        ret    = false;

    if (!(parser = json_parser_new())) {
        LOG_ERROR("Failed to new a object with type JsonParser");
        return false;
    }

    if (json_parser_load_from_file(parser, (const gchar*)config_path.c_str(), &error)) {
        if (!(root = json_parser_get_root(parser))) {
            LOG_ERROR("Failed to get root node from JsonParser");
            goto done;
        }

        if (JSON_NODE_HOLDS_OBJECT(root)) {
            if (!(object = json_node_get_object(root))) {
                LOG_ERROR("Failed to get object from JsonNode");
                goto done;
            }

            if (json_object_has_member(object, "uri")) {
                std::string uri((const char*)json_object_get_string_member(object, "uri"));
                LOG_INFO("uri: {}", uri);
                config.uri = uri;
            }

            if (json_object_has_member(object, "file-loop")) {
                bool f = json_object_get_boolean_member(object, "file-loop");
                LOG_INFO("file-loop: {}", f ? "true" : "false");
                config.file_loop = f;
            }

            if (json_object_has_member(object, "rtsp-latency")) {
                int r = json_object_get_int_member(object, "rtsp-latency");
                LOG_INFO("rtsp-latency: {}", r);
                config.rtsp_latency = r;
            }

            if (json_object_has_member(object, "rtp-protocol")) {
                int r = json_object_get_int_member(object, "rtp-protocol");
                LOG_INFO("rtp-protocol: {}", r);
                config.rtp_protocol = r;
            }

            if (json_object_has_member(object, "display")) {
                JsonObject* d = json_object_get_object_member(object, "display");

                if (json_object_has_member(d, "enable")) {
                    bool e = json_object_get_boolean_member(d, "enable");
                    LOG_INFO("enable-display: {}", e ? "true" : "false");
                    config.enable_display = e;
                }

                if (json_object_has_member(d, "sync")) {
                    bool s = json_object_get_boolean_member(d, "sync");
                    LOG_INFO("sync: {}", s ? "true" : "false");
                    config.sync = s;
                }

                if (json_object_has_member(d, "left")) {
                    int x = json_object_get_int_member(d, "left");
                    LOG_INFO("display-left: {}", x);
                    config.window_x = x;
                }

                if (json_object_has_member(d, "top")) {
                    int y = json_object_get_int_member(d, "top");
                    LOG_INFO("display-top: {}", y);
                    config.window_y = y;
                }

                if (json_object_has_member(d, "width")) {
                    int w = json_object_get_int_member(d, "width");
                    LOG_INFO("display-width: {}", w);
                    config.window_width = w;
                }

                if (json_object_has_member(d, "height")) {
                    int h = json_object_get_int_member(d, "height");
                    LOG_INFO("display-height: {}", h);
                    config.window_height = h;
                }
            }

            if (json_object_has_member(object, "output")) {
                JsonObject* d = json_object_get_object_member(object, "output");

                if (json_object_has_member(d, "enable")) {
                    bool e = json_object_get_boolean_member(d, "enable");
                    LOG_INFO("enable-appsink: {}", e ? "true" : "false");
                    config.enable_appsink = e;
                }

                if (json_object_has_member(d, "memory-type")) {
                    int m = json_object_get_int_member(d, "memory-type");
                    LOG_INFO("memory-type: {}", m);
                    config.cvt_memory_type = m;
                }

                if (json_object_has_member(d, "format")) {
                    std::string f((const char*)json_object_get_string_member(d, "format"));
                    LOG_INFO("format: {}", f);
                    config.cvt_format = f;
                }
            }
        }
    } else {
        LOG_ERROR("Failed to parse json string {}, {}", error->message, config_path.c_str());
        g_error_free(error);
        goto done;
    }

    ret = true;

done:
    g_object_unref(parser);

    return ret;
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

    g_setenv("GST_DEBUG_DUMP_DOT_DIR", "/home/ricardo/workSpace/gstreamer-example/ai_integration/", true);

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
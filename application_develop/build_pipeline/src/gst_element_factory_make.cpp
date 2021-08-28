/*
 * @Description: Build GstPipeline with GstElementFactory.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:25
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 03:39:18
 */

#include <gflags/gflags.h>
#include <sys/stat.h>

#include "VideoPipeline.h"

static GMainLoop* g_main_loop = NULL;

static bool validateSrcUri(const char* name, const std::string& value) { 
    if (!value.compare("")) {
        LOG_ERROR_MSG ("Source Uri required!");
        return false;
    }

    struct stat statbuf;
    if (!stat(value.c_str(), &statbuf)) {
        LOG_INFO_MSG ("Found config file: %s", value.c_str());
        return true;
    }

    LOG_ERROR_MSG ("Invalid config file.");
    return false;
}

DEFINE_string(srcuri, "", "algorithm library with APIs: alg{Init/Proc/Ctrl/Fina}");
DEFINE_validator(srcuri, &validateSrcUri);

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags (&argc, &argv, true);
    VideoPipelineConfig m_vpConfig;
    VideoPipeline *m_vp;

    gst_init(&argc, &argv);

    if (!(g_main_loop = g_main_loop_new (NULL, FALSE))) {
        LOG_ERROR_MSG ("Failed to new a object with type GMainLoop");
        goto exit;
    }

    m_vpConfig.src = FLAGS_srcuri;

    m_vp = new VideoPipeline(m_vpConfig);

    if (!m_vp->Create()) {
        LOG_ERROR_MSG ("Pipeline Create failed.");
        goto exit;
    }

    m_vp->Start();

    g_main_loop_run (g_main_loop);

exit:
    if (g_main_loop) g_main_loop_unref (g_main_loop);

    if (m_vp) {
        m_vp->Destroy();
        delete m_vp;
        m_vp = NULL;
    }

    google::ShutDownCommandLineFlags ();
    return 0;
}
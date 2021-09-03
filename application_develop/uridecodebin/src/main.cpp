/*
 * @Description: Test Program.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:17:16
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-09-03 23:54:26
 */

#include <gflags/gflags.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>

#include "VideoPipeline.h"
#include "DoubleBufferCache.h"

static GMainLoop* g_main_loop = NULL;

static bool validateSrcUri (const char* name, const std::string& value) { 
    if (!value.compare("")) {
        LOG_ERROR_MSG ("Source Uri required!");
        return false;
    }

    int pos = value.find("//");

    std::string uri_type = value.substr(0, pos - 1);
    std::string uri_path = value.substr(pos);

    if (!uri_type.compare ("file:")) {  // make sure file exist.
        struct stat statbuf;
        if (!stat(uri_path.c_str(), &statbuf)) {
            LOG_INFO_MSG ("Found config file: %s", value.substr(pos).c_str());
            return true;
        }
    } else {
        return true;
    }
    
    LOG_ERROR_MSG ("Invalid config file.");
    return false;
}

DEFINE_string (srcuri, "", "algorithm library with APIs: alg{Init/Proc/Ctrl/Fina}");
DEFINE_validator (srcuri, &validateSrcUri);

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags (&argc, &argv, true);

    VideoPipelineConfig m_vpConfig;
    VideoPipeline *m_vp;
    std::ostringstream m_pipelineCmd;
    std::string m_strPipeline;

    gst_init(&argc, &argv);

    if (!(g_main_loop = g_main_loop_new (NULL, FALSE))) {
        LOG_ERROR_MSG ("Failed to new a object with type GMainLoop");
        goto exit;
    }

    m_vpConfig.src = FLAGS_srcuri;
    m_vpConfig.turbo = true;
    m_vpConfig.skip_frame = true;

    m_vp = new VideoPipeline(m_vpConfig);

    if (!m_vp->Create()) {
        LOG_ERROR_MSG ("Pipeline Create failed: lack of elements");
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
/*
 * @Description: Test Program.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:17:16
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-29 13:37:20
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

    struct stat statbuf;
    if (!stat(value.c_str(), &statbuf)) {
        LOG_INFO_MSG ("Found config file: %s", value.c_str());
        return true;
    }

    LOG_ERROR_MSG ("Invalid config file.");
    return false;
}

DEFINE_string (srcuri, "", "algorithm library with APIs: alg{Init/Proc/Ctrl/Fina}");
DEFINE_validator (srcuri, &validateSrcUri);

void putData (std::shared_ptr<cv::Mat> img, void* user_data)
{
    // LOG_INFO_MSG ("putData called");

    DoubleBufCache<cv::Mat>* db =
        reinterpret_cast<DoubleBufCache<cv::Mat>*> (user_data);

    std::string sinkwords ("appsink");
    cv::Point fontpos= cv::Point (100, 115);
    cv::Scalar fongcolor(150, 255, 40);
    cv::putText(*img, sinkwords, fontpos, cv::FONT_HERSHEY_COMPLEX,
                    0.8, fongcolor, 2, 0.3);
    cv::Rect rect (100, 100, 1720, 880);
    cv::Scalar rectcolor(0, 200, 0);
    cv::rectangle (*img, rect, rectcolor, 3);

    db->feed(img);
}

std::shared_ptr<cv::Mat> getData (void* user_data)
{
    // LOG_INFO_MSG ("getData called");

    DoubleBufCache<cv::Mat>* db =
        reinterpret_cast<DoubleBufCache<cv::Mat>*> (user_data);
    std::shared_ptr<cv::Mat> img;

    img = db->fetch();

    std::string srcwords ("appsrc");
    cv::Point fontpos= cv::Point (1700, 970);
    cv::Scalar fongcolor(150, 255, 40);
    cv::putText(*img, srcwords, fontpos, cv::FONT_HERSHEY_COMPLEX,
                    0.8, fongcolor, 2, 0.3);
    cv::Rect rect (110, 110, 1720, 880);
    cv::Scalar rectcolor(0, 0, 200);
    cv::rectangle (*img, rect, rectcolor, 3);

    return img;
}

std::shared_ptr<cv::Rect> getResult(void* user_data)
{

}

void procData(GstBuffer* buffer, const std::shared_ptr<cv::Rect>& rect)
{

}

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags (&argc, &argv, true);
    VideoPipelineConfig       m_vpConfig;
    VideoPipeline*            m_vp;
    SinkPutDataFunc           m_putDataFunc;
    ProbeGetResultFunc        m_getResultFunc;
    ProcDataFunc              m_procDataFunc;
    DoubleBufCache<cv::Mat>*  m_dataBufferCache;
    DoubleBufCache<cv::Rect>* m_resultBufferCache;

    gst_init(&argc, &argv);

    if (!(g_main_loop = g_main_loop_new (NULL, FALSE))) {
        LOG_ERROR_MSG ("Failed to new a object with type GMainLoop");
        goto exit;
    }

    m_vpConfig.src = FLAGS_srcuri;
    m_vpConfig.conv_format = "BGR";
    m_vpConfig.conv_width = 1920;
    m_vpConfig.conv_height = 1080;

    m_vp = new VideoPipeline(m_vpConfig);

    m_putDataFunc = std::bind(putData,
                            std::placeholders::_1, std::placeholders::_2);
    m_getResultFunc = std::bind(getResult, std::placeholders::_1);
    m_procDataFunc = std::bind(procData,
                            std::placeholders::_1, std::placeholders::_2);
    m_dataBufferCache = new DoubleBufCache<cv::Mat> ();
    m_resultBufferCache = new DoubleBufCache<cv::Rect> ();

    m_vp->SetCallbacks (m_putDataFunc, m_dataBufferCache);
    m_vp->SetCallbacks (m_getResultFunc, m_resultBufferCache);
    m_vp->SetCallbacks (m_procDataFunc, NULL);

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

    if (m_dataBufferCache) {
        delete m_dataBufferCache;
        m_dataBufferCache = NULL;
    }

    if (m_resultBufferCache) {
        delete m_resultBufferCache;
        m_dataBufferCache = NULL;
    }

    google::ShutDownCommandLineFlags ();
    return 0;
}
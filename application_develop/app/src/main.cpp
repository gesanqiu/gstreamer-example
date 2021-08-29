/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:17:16
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-29 12:22:48
 */

#include <gflags/gflags.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>

#include "appsink.h"
#include "appsrc.h"
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

int main(int argc, char* argv[])
{
    google::ParseCommandLineFlags (&argc, &argv, true);
    SinkPipelineConfig       m_sinkConfig;
    SinkPipeline*            m_sinkPipeline;
    SrcPipelineConfig        m_srcCofig;
    SrcPipeline*             m_srcPipeline;
    SinkPutDataFunc          m_sinkPutDataFunc;
    SrcGetDataFunc           m_srcGetDataFunc;
    DoubleBufCache<cv::Mat>* m_bufferCache;

    gst_init(&argc, &argv);

    if (!(g_main_loop = g_main_loop_new (NULL, FALSE))) {
        LOG_ERROR_MSG ("Failed to new a object with type GMainLoop");
        goto exit;
    }

    m_sinkConfig.src = FLAGS_srcuri;
    m_sinkConfig.conv_format = "BGR";
    m_sinkConfig.conv_width = 1920;
    m_sinkConfig.conv_height = 1080;

    m_srcCofig.src_format = "BGR";
    m_srcCofig.src_width = 1920;
    m_srcCofig.src_height = 1080;
    m_srcCofig.conv_format = "NV12";
    m_srcCofig.conv_width = 1920;
    m_srcCofig.conv_height = 1080;

    m_sinkPipeline = new SinkPipeline(m_sinkConfig);
    m_srcPipeline = new SrcPipeline(m_srcCofig);

    m_sinkPutDataFunc = std::bind(putData,
                            std::placeholders::_1, std::placeholders::_2);
    m_srcGetDataFunc = std::bind(getData, std::placeholders::_1);
    m_bufferCache = new DoubleBufCache<cv::Mat> ();

    m_sinkPipeline->SetCallbacks(m_sinkPutDataFunc, m_bufferCache);
    m_srcPipeline->SetCallbacks(m_srcGetDataFunc, m_bufferCache);

    if (!m_sinkPipeline->Create()) {
        LOG_ERROR_MSG ("Pipeline Create failed.");
        goto exit;
    }

    if (!m_srcPipeline->Create()) {
        LOG_ERROR_MSG ("Pipeline Create failed.");
        goto exit;
    }

    m_sinkPipeline->Start();
    sleep(3);
    m_srcPipeline->Start();

    g_main_loop_run (g_main_loop);

exit:
    if (g_main_loop) g_main_loop_unref (g_main_loop);

    if (m_sinkPipeline) {
        m_sinkPipeline->Destroy();
        delete m_sinkPipeline;
        m_sinkPipeline = NULL;
    }

    if (m_srcPipeline) {
        m_srcPipeline->Destroy();
        delete m_srcPipeline;
        m_srcPipeline = NULL;
    }
    
    if (m_bufferCache) {
        delete m_bufferCache;
        m_bufferCache = NULL;
    }

    google::ShutDownCommandLineFlags ();
    return 0;
}
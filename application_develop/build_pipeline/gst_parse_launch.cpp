/*
 * @Description: Build GstPipeline with GstParse.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:04
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-27 12:20:35
 */
#include "VideoPipeline.h"

static GMainLoop* g_main_loop = NULL;

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);
    g_main_loop_new (NULL, FALSE);

    VideoPipelineConfig m_vpConfig;

    VideoPipeline *m_vp = new VideoPipeline(m_vpConfig);

    std::string m_pipelineCmd ("");

    m_vp->SetPipeline(m_pipelineCmd);

    m_vp->Create();

    m_vp->Start();

    g_main_loop_run (g_main_loop);

done:
    if (g_main_loop) g_main_loop_unref (g_main_loop);

    if (m_vp) {
        m_vp->Destroy();
        delete m_vp;
        m_vp = NULL;
    }
    return 0;
}
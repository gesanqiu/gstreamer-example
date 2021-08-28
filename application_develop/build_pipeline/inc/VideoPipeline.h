/*
 * @Description: GstPipeline common header.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 03:23:12
 */
#pragma once

#include "Common.h"

typedef struct _VideoPipelineConfig {
    std::string src;
}VideoPipelineConfig;

class VideoPipeline
{
public:
    VideoPipeline (const VideoPipelineConfig& config);
    bool Create   (void);
    bool Start    (void);
    bool Pause    (void);
    bool Resume   (void);
    void Destroy  (void);
#ifdef PARSE_LAUNCH
    bool SetPipeline (std::string& pipeline);
#endif
    ~VideoPipeline(void);

private:
    VideoPipelineConfig m_config;
    GstElement* m_gstPipeline;

#ifdef PARSE_LAUNCH
    std::string m_pipeline;
#endif

#ifdef FACTORY_MAKE
    GstElement* m_source;
    GstElement* m_qtdemux;
    GstElement* m_h264parse;
    GstElement* m_decoder;
    GstElement* m_display;
#endif
};

/*
gst-launch-1.0 filesrc location=test.mp4 ! qtdemux ! qtivdec ! waylandsink
*/
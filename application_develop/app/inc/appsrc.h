/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 10:06:03
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-29 12:04:27
 */
#pragma once

#include "Common.h"

typedef struct _SrcPipelineConfig {
    std::string src_format;
    int         src_width;
    int         src_height;
    /*-------------videoconvert-------------*/
    std::string conv_format;
    int         conv_width;
    int         conv_height;
}SrcPipelineConfig;

class SrcPipeline
{
public:
    SrcPipeline       (const SrcPipelineConfig& config);
    bool Create       (void);
    bool Start        (void);
    bool Pause        (void);
    bool Resume       (void);
    void Destroy      (void);
    void SetCallbacks (SrcGetDataFunc func, void* args);
    ~SrcPipeline      (void);

public:
    SrcGetDataFunc  m_getDataFunc;
    void*           m_getDataArgs;
    uint64_t        m_timestamp;

private:
    SrcPipelineConfig m_config;

    GstElement* m_srcPipeline;
    GstElement* m_appsrc;
    GstElement* m_videoconv;
    GstElement* m_capfilter;
    GstElement* m_display;
};

/*
Decode Pipeline: filesrc location=test.mp4 ! qtdemux ! qtivdec ! appsink
Display Pipeline: appsrc ! videoconvert ! waylandsink
*/
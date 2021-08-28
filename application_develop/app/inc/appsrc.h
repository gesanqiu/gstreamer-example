/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 10:06:03
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 11:49:30
 */
#pragma once

#include "Common.h"

typedef struct _SrcPipelineConfig {
    std::string src;

    /*-----------qtivtransform-------------*/
    std::string sink_format;
    int         sink_width;
    int         sink_height;
}SrcPipelineConfig;

class SrcPipeline
{
public:
    SrcPipeline  (const SrcPipelineConfig& config);
    bool Create  (void);
    bool Start   (void);
    bool Pause   (void);
    bool Resume  (void);
    void Destroy (void);
    void SetCallbacks (SinkPutDataFunc func, void* args);
    void SetCallbacks (SrcGetDataFunc func, void* args);
    ~SrcPipeline (void);

private:
    SrcPipelineConfig m_config;

    GstElement* m_srcPipeline;
    GstElement* m_appsrc;
    GstElement* m_videoconv;
    GstElement* m_display;
};

/*
Decode Pipeline: filesrc location=test.mp4 ! qtdemux ! qtivdec ! appsink
Display Pipeline: appsrc ! videoconvert ! waylandsink
*/
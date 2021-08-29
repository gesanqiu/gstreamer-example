/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 10:05:59
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-29 11:55:46
 */
#pragma once

#include "Common.h"

typedef struct _SinkPipelineConfig {
    std::string src;

    /*-------------waylandsink-------------*/
    std::string conv_format;
    int         conv_width;
    int         conv_height;
}SinkPipelineConfig;

class SinkPipeline
{
public:
    SinkPipeline      (const SinkPipelineConfig& config);
    bool Create       (void);
    bool Start        (void);
    bool Pause        (void);
    bool Resume       (void);
    void Destroy      (void);
    void SetCallbacks (SinkPutDataFunc func, void* args);
    ~SinkPipeline     (void);

public:
    SinkPutDataFunc m_putDataFunc;
    void*           m_putDataArgs;

private:
    SinkPipelineConfig m_config;

    GstElement* m_sinkPipeline;
    GstElement* m_source;
    GstElement* m_qtdemux;
    GstElement* m_h264parse;
    GstElement* m_decoder;
    GstElement* m_qtivtrans;
    GstElement* m_capfilter;
    GstElement* m_appsink;
};

/*
Decode Pipeline: filesrc location=test.mp4 ! qtdemux ! qtivdec ! appsink
Display Pipeline: appsrc ! videoconvert ! waylandsink
*/
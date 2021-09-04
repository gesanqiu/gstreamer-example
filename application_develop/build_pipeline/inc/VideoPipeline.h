/*
 * @Description: GstPipeline common header.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-31 14:06:46
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

public:
    VideoPipelineConfig m_config;
    GstElement* m_gstPipeline;

#ifdef PARSE_LAUNCH
    std::string m_pipeline;
#endif

#ifdef FACTORY_MAKE
    GstElement* m_source;
    GstElement* m_qtdemux;
    GstElement* m_queue0;
    GstElement* m_queue1;
    GstElement* m_h264parse;
    GstElement* m_vdecoder;
    GstElement* m_display;
    GstElement* m_aacparse;
    GstElement* m_adecoder;
    GstElement* m_audioConv;
    GstElement* m_audioReSample;
    GstElement* m_player;
#endif
};

/*
 * gst-launch-1.0 filesrc location=test.mp4 ! qtdemux name=demux demux. ! \
 * queue ! h264parse ! qtivdec ! waylandsink demux. ! queue ! aacparse ! \
 * avdec_aac ! audioconvert ! audioresample ! pulsesink volume=1
*/
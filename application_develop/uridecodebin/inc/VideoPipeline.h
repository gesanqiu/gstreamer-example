/*
 * @Description: GstPipeline common header.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-09-01 12:53:17
 */
#pragma once

#include "Common.h"

typedef struct _VideoPipelineConfig {
    std::string src;
    /*---------------qtivdec---------------*/
    bool        turbo;
    bool        skip_frame;
}VideoPipelineConfig;

class VideoPipeline
{
public:
    VideoPipeline     (const VideoPipelineConfig& config);
    bool Create       (void);
    bool Start        (void);
    bool Pause        (void);
    bool Resume       (void);
    void Destroy      (void);
    ~VideoPipeline    (void);

public:
    VideoPipelineConfig m_config;
    GstElement* m_gstPipeline;

    GstElement* m_source;
    GstElement* m_qtdemux;
    GstElement* m_h264parse;
    GstElement* m_decoder;
    GstElement* m_display;
    GstElement* m_appsink;
};

/*
* gst-launch-1.0 uridecodebin uri=file:///absolute/path/video.mp4 ! waylandsink
* or
* gst-launch-1.0 filesrc location=video.mp4 ! qtdemux ! \
* multiqueue ! h264parse ! capfilter ! qtivdec ! waylandsink
*/
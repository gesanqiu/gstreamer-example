/*
 * @Description: GstPipeline common header.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:39
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2021-09-03 23:22:24
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
    GstElement* m_demux;
    GstElement* m_h264parse;
    GstElement* m_vdecoder;
    GstElement* m_adecoder;
    GstElement* m_videoConv;
    GstElement* m_display;
    GstElement* m_audioConv;
    GstElement* m_audioReSample;
    GstElement* m_player;
};

/*
* gst-launch-1.0 uridecodebin uri=file:///absolute/path/video.mp4 ! waylandsink
* or
* gst-launch-1.0 filesrc location=video.mp4 ! qtdemux ! \
* multiqueue ! h264parse ! capfilter ! qtivdec ! waylandsink
*/

/*
 * gst-launch-1.0 \
 * uridecodebin uri=file:///absolute/path/video.mp4 demux. ! \
 * queue ! videoconvert ! waylandysink demux. ! queue ! audioconvert ! pulsesink volume=1
 * 
 */
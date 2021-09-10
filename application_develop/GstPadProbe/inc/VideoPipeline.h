/*
 * @Description: GstPipeline common header.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 08:11:39
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-09-10 03:26:45
 */
#pragma once

#include "Common.h"

typedef struct _VideoPipelineConfig {
    std::string src;
    /*-------------qtivtransform-------------*/
    std::string conv_format;
    int         conv_width;
    int         conv_height;
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
    void SetCallbacks (SinkPutDataFunc func, void* args);
    void SetCallbacks (ProbeGetResultFunc func, void* args);
    void SetCallbacks (ProcDataFunc func, void* args);
    ~VideoPipeline    (void);

public:
    SinkPutDataFunc     m_putDataFunc;
    void*               m_putDataArgs;
    ProbeGetResultFunc  m_getResultFunc;
    void*               m_getResultArgs;
    ProcDataFunc        m_procDataFunc;
    void*               m_procDataArgs;

    unsigned long       m_queue0_probe;
    unsigned long       m_trans_sink_probe;
    unsigned long       m_trans_src_probe;

    VideoPipelineConfig m_config;
    GstElement*         m_gstPipeline;

    volatile gint       m_syncCount;
    volatile gboolean   isExited;
    GMutex              m_syncMuxtex;
    GCond               m_syncCondition;
    GMutex              m_mutex;

    GstElement*         m_source;
    GstElement*         m_qtdemux;
    GstElement*         m_rtph264depay;
    GstElement*         m_h264parse;
    GstElement*         m_decoder;
    GstElement*         m_tee;
    GstPad*             m_teeDisplayPad;
    GstPad*             m_teeAppsinkPad;
    GstElement*         m_queue0;
    GstElement*         m_qtioverlay;
    GstElement*         m_display;
    GstElement*         m_queue1;
    GstElement*         m_qtivtrans;
    GstElement*         m_capfilter;
    GstElement*         m_appsink;
};

/*
gst-launch-1.0 filesrc location=video.mp4 ! qtdemux ! h264parse ! qtivdec ! 
tee name=t1 t1. ! queue ! qtioverlay meta-color=true ! waylandsink t1. ! queue ! qtivtransform ! 
video/x-raw,format=BGR,width=1920,height=1080 ! appsink
*/
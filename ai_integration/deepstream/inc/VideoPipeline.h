/*
 * @Description: Implement of VideoPipeline on DeepStream.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2022-07-15 22:07:29
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-07-16 18:41:49
 */
#pragma once

# include "Common.h"

static int pipeline_id = 0;

typedef enum _VideoType {
    FILE_STREAM = 0,
    RTSP_STREAM
}VideoType;

typedef struct _VideoPipelineConfig {
    /*---------------uridecodebin---------------*/
    VideoType   video_type { VideoType::FILE_STREAM };
    bool        file_loop;
    int         rtsp_latency;
    int         rtp_protocol;
    std::string uri;
    /*---------------display branch---------------*/
    bool        enable_display;
    bool        sync;
    int         window_x;
    int         window_y;
    int         window_width;
    int         window_height;
    /*---------------output branch---------------*/
    bool        enable_appsink;
    /*---------------nvvideoconvert---------------*/
    int         cvt_memory_type;
    std::string cvt_format;
}VideoPipelineConfig;

class VideoPipeline {
public:
    VideoPipeline      (const VideoPipelineConfig& config);
    ~VideoPipeline     ();
    bool Create        ();
    bool Start         ();
    bool Pause         ();
    bool Resume        ();
    void Destroy       ();
    void SetCallbacks  (PutFrameFunc func, void* args);
    void SetCallbacks  (GetResultFunc func, void* args);
    void SetCallbacks  (ProcResultFunc func);

    PutFrameFunc        m_putFrameFunc;
    void*               m_putFrameArgs;
    GetResultFunc       m_getResultFunc;
    void*               m_getResultArgs;
    ProcResultFunc      m_procResultFunc;


    uint64_t            m_queue0_src_probe;      /* probe before nvdsosd */
    uint64_t            m_cvt_sink_probe;
    uint64_t            m_cvt_src_probe;
    uint64_t            m_dec_sink_probe;

    uint64_t            m_prev_accumulated_base;
    uint64_t            m_accumulated_base;

    VideoPipelineConfig m_config;

    volatile int        m_syncCount;
    volatile bool       m_isExited;
    GMutex              m_syncMuxtex;
    GCond               m_syncCondition;
    GMutex              m_mutex;
    bool                m_dump;

    GstElement*         m_pipeline;
    GstElement*         m_source;           /* uridecodebin */
    GstElement*         m_muxer;            /* nvstreamuxer */
    GstElement*         m_decoder;          /* nvv4l2decoder */
    GstElement*         m_tee;
    GstElement*         m_queue0;           /* for display branch */ 
    GstElement*         m_fakesink;         /* sync stream when disabled display */
    GstElement*         m_nvvideoconvert0;
    GstElement*         m_capfilter0;
    GstElement*         m_nvdsosd;
    GstElement*         m_display;          /* nveglglessink */
    GstElement*         m_queue1;           /* for output branch */
    GstElement*         m_nvvideoconvert1;
    GstElement*         m_capfilter1;
    GstElement*         m_appsink;
};


/*

gst-launch-1.0 uridecodebin uri="file:///home/ricardo/workSpace/gstreamer-example/ai_integration/test.mp4" ! \
tee name=t1 ! queue ! nvvideoconvert ! 'video/x-raw(memory:NVMM),format=(string)RGBA' ! nvdsosd ! nveglglessink \
t1. ! queue ! nvvideoconvert ! 'video/x-raw(memory:NVMM),format=(string)RGBA' ! appsink

*/
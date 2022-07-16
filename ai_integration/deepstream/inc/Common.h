/*
 * @Description: Common Utils.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:24:25
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2022-07-16 13:49:59
 */
#pragma once

#include <iostream>
#include <string>
#include <memory>
#include <functional>
#include <unistd.h>
#include <vector>

#include <opencv2/opencv.hpp>
#include <gst/gst.h>
#include <gst/app/app.h>

#include "Logger.h"

class OSDObject {
public:
    int x, y, width, height;
    double r, g, b, a;

    OSDObject(int _x, int _y, int _width, int _height,
        int _r, int _g, int _b, int _a = 1.0) : 
        x(_x), y(_y), width(_width), height(_height),
        r(_r), g(_g), b(_b), a(_a) {

    }
};

class GstBufferObject {
public:
    GstBufferObject(GstBuffer* buffer) {
        if(buffer) {
            gst_buffer_ref(buffer);
            m_buffer  = buffer;
        }
    }

   ~GstBufferObject() {
        if (m_buffer) {
            gst_buffer_unref(m_buffer);
            m_buffer = nullptr;
        }
    }

    GstBuffer* GetBuffer() {
        return m_buffer;
    }

    GstBuffer* RefBuffer() {
        if (m_buffer) {
            gst_buffer_ref(m_buffer);
        }

        return m_buffer;
    }

private:
    GstBuffer* m_buffer;
};

class GstSampleObject {
public:
    GstSampleObject(GstSample* sample, uint64_t timestamp) :
        m_sample   (sample),
        m_timestamp(timestamp),
        m_buffer   (nullptr),
        m_format   (nullptr),
        m_rows     (0),
        m_cols     (0),
        m_fpsn     (0),
        m_fpsd     (0) {

        }

   ~GstSampleObject() {
        if(m_sample) {
            gst_sample_unref(m_sample);
            m_sample = nullptr;
        }
    }

    GstSample* GetSample() {
        return m_sample;
    }

    GstSample* RefSample() {
        return gst_sample_ref(m_sample);
    }

    GstBuffer* GetBuffer(int& width, int& height, std::string& format) {
        if (!m_buffer) {
            GstCaps* caps = gst_sample_get_caps(m_sample);
            GstStructure* structure = gst_caps_get_structure(caps, 0);
            gst_structure_get_int(structure, "width",  &m_cols);
            gst_structure_get_int(structure, "height", &m_rows);
            gst_structure_get_fraction(structure, "framerate", &m_fpsn, &m_fpsd);
            m_format = gst_structure_get_string(structure, "format");
            m_buffer = gst_sample_get_buffer(m_sample);
        }

        width  = m_cols;
        height = m_rows;
        format = m_format;
        return m_buffer;
    }

    GstBuffer* RefBuffer(int& width, int& height, std::string& format) {
        return gst_buffer_ref(GetBuffer(width, height, format));
    }

    gint64 GetTimestamp() {
        return m_timestamp;
    }

private:

    GstSample* m_sample;
    GstBuffer* m_buffer;

    int         m_cols;
    int         m_rows;
    int         m_fpsn;
    int         m_fpsd;
    const char* m_format;
    uint64_t    m_timestamp;
};

// callback functions
typedef std::function<bool(GstSample* , void*)> PutFrameFunc;

typedef std::function<std::shared_ptr<GstSampleObject>(void*)> GetFrameFunc;

typedef std::function<bool(std::shared_ptr<std::vector<OSDObject> >, void*)> PutResultFunc;

typedef std::function<std::shared_ptr<std::vector<OSDObject> >(void*)> GetResultFunc;

typedef std::function<void(GstBuffer* buffer, const std::shared_ptr<std::vector<OSDObject> >& results)> ProcResultFunc;
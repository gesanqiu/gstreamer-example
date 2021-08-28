/*
 * @Description: Common Utils.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:24:25
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 11:48:17
 */
#pragma once

#include <iostream>
#include <string>
#include <memory>
#include <functional>

#include <opencv2/opencv.hpp>
#include <gst/gst.h>

#define LOG_ERROR_MSG(msg, ...)  \
    g_print("** ERROR: <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO_MSG(msg, ...) \
    g_print("** INFO:  <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN_MSG(msg, ...) \
    g_print("** WARN:  <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

typedef std::function<void(GstSample*, void*)> SinkPutDataFunc;
typedef std::function<cv::Mat(void*)> SrcGetDataFunc;

/*
 * @Description: Common Utils.
 * @version: 1.0
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-27 12:24:25
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 03:18:09
 */
#pragma once

#include <iostream>
#include <string>
#include <memory>

#include <gst/gst.h>

#define LOG_ERROR_MSG(msg, ...)  \
    g_print("** ERROR: <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define LOG_INFO_MSG(msg, ...) \
    g_print("** INFO:  <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

#define LOG_WARN_MSG(msg, ...) \
    g_print("** WARN:  <%s:%s:%d>: " msg "\n", __FILE__, __func__, __LINE__, ##__VA_ARGS__)

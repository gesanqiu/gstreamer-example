/*
 * @Description: 
 * @version: 
 * @Author: Ricardo Lu<shenglu1202@163.com>
 * @Date: 2021-08-28 09:17:16
 * @LastEditors: Ricardo Lu
 * @LastEditTime: 2021-08-28 11:05:18
 */

#include <gflags/gflags.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>

#include "appsink.h"
#include "appsrc.h"

static GMainLoop* g_main_loop = NULL;

static bool validateSrcUri(const char* name, const std::string& value) { 
    if (!value.compare("")) {
        LOG_ERROR_MSG ("Source Uri required!");
        return false;
    }

    struct stat statbuf;
    if (!stat(value.c_str(), &statbuf)) {
        LOG_INFO_MSG ("Found config file: %s", value.c_str());
        return true;
    }

    LOG_ERROR_MSG ("Invalid config file.");
    return false;
}

DEFINE_string(srcuri, "", "algorithm library with APIs: alg{Init/Proc/Ctrl/Fina}");
DEFINE_validator(srcuri, &validateSrcUri);

int main(int argc, char* argv[])
{

}
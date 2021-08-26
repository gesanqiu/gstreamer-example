/*
* Copyright (c) 2016-2020, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define LOG_TAG "Overlay"

#include <utils/Log.h>
#include <algorithm>
#include <fcntl.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <media/msm_media_info.h>
#include <string.h>
#include <cstring>
#include <string>
#include <assert.h>
#include <sys/time.h>
#include <chrono>
#include <vector>
#include <math.h>
#include <fstream>
#if USE_SKIA
#include <SkSurface.h>
#include <SkString.h>
#include <SkBitmap.h>
#include <SkBlurMaskFilter.h>
#endif

#include "common/utils/qmmf_tools.h"
#include "qmmf-sdk/qmmf_overlay.h"
#include "qmmf_overlay_item.h"

namespace qmmf {

namespace overlay {

using namespace android;
using namespace std;

#define ROUND_TO(val, round_to) ((val + round_to - 1) & ~(round_to - 1))

#ifdef OVERLAY_OPEN_CL_BLIT

cl_device_id OpenClKernel::device_id_ = nullptr;
cl_context OpenClKernel::context_ = nullptr;
cl_command_queue OpenClKernel::command_queue_ = nullptr;
std::mutex OpenClKernel::lock_;
int32_t OpenClKernel::ref_count = 0;

int32_t OpenClKernel::OpenCLInit () {
  ref_count++;
  if (ref_count > 1) {
      return 0;
  }

  OVDBG_VERBOSE("%s: Enter ", __func__);

  cl_context_properties properties[] = {CL_CONTEXT_PLATFORM, 0, 0};
  cl_platform_id plat = 0;
  cl_uint ret_num_platform = 0;
  cl_uint ret_num_devices = 0;
  cl_int cl_err;

  cl_err = clGetPlatformIDs(1, &plat, &ret_num_platform);
  if ((CL_SUCCESS != cl_err) || (ret_num_platform == 0)) {
    OVDBG_ERROR("%s: Open cl hw platform not available. rc %d", __func__, cl_err);
    return BAD_VALUE;
  }

  properties[1] = (cl_context_properties)plat;

  cl_err = clGetDeviceIDs(plat, CL_DEVICE_TYPE_DEFAULT, 1, &device_id_,
                          &ret_num_devices);
  if ((CL_SUCCESS != cl_err) || (ret_num_devices != 1)) {
    OVDBG_ERROR("%s: Open cl hw device not available. rc %d", __func__, cl_err);
    return BAD_VALUE;
  }

  context_ = clCreateContext(properties, 1, &device_id_, NULL, NULL, &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to create Open cl context. rc: %d", __func__,
        cl_err);
    return BAD_VALUE;
  }

  command_queue_ = clCreateCommandQueueWithProperties(context_, device_id_, 0,
      &cl_err);
  if (CL_SUCCESS != cl_err) {
    clReleaseContext(context_);
    OVDBG_ERROR("%s: Failed to create Open cl command queue. rc: %d", __func__,
        cl_err);
    return BAD_VALUE;
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::OpenCLDeInit () {
  ref_count--;
  if (ref_count > 0) {
    return 0;
  } else if (ref_count < 0) {
    OVDBG_ERROR("%s: Instance is already destroyed.", __func__);
    return -1;
  }

  OVDBG_VERBOSE("%s: Enter ", __func__);

  assert(context_ != nullptr);

  if (command_queue_) {
    clReleaseCommandQueue(command_queue_);
    command_queue_ = nullptr;
  }

  if (context_) {
    clReleaseContext(context_);
    context_ = nullptr;
  }

  if (device_id_) {
    clReleaseDevice(device_id_);
    device_id_ = nullptr;
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

/* This initializes Open CL context and command queue, loads and builds Open CL
 * program. This is reference instance which  cannot be use by itself because
 * there is no kernel instance */
std::shared_ptr<OpenClKernel> OpenClKernel::New(const std::string &path_to_src,
    const std::string &name) {

  std::unique_lock<std::mutex> lock(lock_);
  OpenCLInit();

  auto new_instance = std::shared_ptr<OpenClKernel>(new OpenClKernel(name),
      [](void const *) {
        if (ref_count == 1) {
          OpenCLDeInit();
          ref_count--;
        }
      });

  auto ret = new_instance->BuildProgram(path_to_src);
  if (ret) {
    OVDBG_ERROR("%s: Failed to build blit program", __func__);
    return nullptr;
  }

  return new_instance;
}

/* This creates new instance  without loading and building Open CL program.
 * It uses program from reference instance */
std::shared_ptr<OpenClKernel> OpenClKernel::AddInstance() {

  std::unique_lock<std::mutex> lock(lock_);
  OpenCLInit();

  auto new_instance = std::shared_ptr<OpenClKernel>(new OpenClKernel(*this),
    [this](void const *) {
      OpenCLDeInit();
    });

  new_instance->CreateKernelInstance();

  return new_instance;
}

OpenClKernel::~OpenClKernel() {
  /* OpenCL program is created by reference instance which does not have
   * kernel instance. */
  if (kernel_) {
    clReleaseKernel(kernel_);
    kernel_ = nullptr;
  } else if (prog_) {
    clReleaseProgram(prog_);
    prog_ = nullptr;
  }
}

int32_t OpenClKernel::BuildProgram(const std::string &path_to_src) {

  OVDBG_VERBOSE("%s: Enter ", __func__);

  assert(context_ != nullptr);

  if (path_to_src.empty()) {
    OVDBG_ERROR("%s: Invalid input source path! ", __func__);
    return BAD_VALUE;
  }

  std::ifstream src_file(path_to_src);
  if (!src_file.is_open()) {
    OVDBG_ERROR("%s: Fail to open source file: %s ", __func__,
        path_to_src.c_str());
    return BAD_VALUE;
  }

  std::string kernel_src((std::istreambuf_iterator<char>(src_file)),
                         std::istreambuf_iterator<char>());

  cl_int cl_err;
  cl_int num_program_devices = 1;
  const char *strings[] = {kernel_src.c_str()};
  const size_t length = kernel_src.size();
  prog_ = clCreateProgramWithSource(context_, num_program_devices, strings,
                                    &length, &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Fail to create CL program! ",__func__);
    return BAD_VALUE;
  }

  cl_err = clBuildProgram(prog_, num_program_devices, &device_id_,
                          " -cl-fast-relaxed-math -D ARTIFACT_REMOVE ",
                          nullptr, nullptr);
  if (CL_SUCCESS != cl_err) {
    std::string build_log = CreateCLKernelBuildLog();
    OVDBG_ERROR("%s: Failed to build Open cl program. rc: %d",  __func__,
        cl_err);
    OVDBG_ERROR("%s: ---------- Open cl build log ----------\n%s", __func__,
        build_log.c_str());
    return BAD_VALUE;
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::CreateKernelInstance() {

  OVDBG_VERBOSE("%s: Enter ", __func__);

  cl_int cl_err;

  assert(context_ != nullptr);

  kernel_ = clCreateKernel(prog_, kernel_name_.c_str(), &cl_err);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to create Open cl kernel rc: %d", __func__, cl_err);
    return BAD_VALUE;
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

int32_t OpenClKernel::MapBuffer(cl_mem &cl_buffer, void *vaddr, int32_t fd,
    uint32_t size) {

  OVDBG_VERBOSE("%s: Enter addr %p fd %d size %d", __func__, vaddr, fd, size);

  cl_int rc;

  assert(context_ != nullptr);

  cl_mem_flags mem_flags = 0;
  mem_flags |= CL_MEM_READ_WRITE;
  mem_flags |= CL_MEM_USE_HOST_PTR;
  mem_flags |= CL_MEM_EXT_HOST_PTR_QCOM;

  cl_mem_ion_host_ptr ionmem {};
  ionmem.ext_host_ptr.allocation_type = CL_MEM_ION_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_WRITEBACK_QCOM;
  ionmem.ion_hostptr = vaddr;
  ionmem.ion_filedesc = fd;

  cl_buffer = clCreateBuffer( context_,
                              mem_flags,
                              size,
                              mem_flags & CL_MEM_EXT_HOST_PTR_QCOM ? &ionmem : nullptr,
                              &rc);
  if (CL_SUCCESS != rc) {
    OVDBG_ERROR("%s: Cannot create cl buffer memory object! rc %d", __func__, rc);
    return BAD_VALUE;
  }

  return 0;
}

int32_t OpenClKernel::UnMapBuffer(cl_mem &cl_buffer) {
  if (cl_buffer) {
    auto rc = clReleaseMemObject(cl_buffer);
    if (CL_SUCCESS != rc) {
      OVDBG_ERROR("%s: cannot release buf! rc %d", __func__, rc);
      return BAD_VALUE;
    }
    cl_buffer = nullptr;
  }

  return 0;
}

// todo: add format as input argument
int32_t OpenClKernel::MapImage(cl_mem &cl_buffer, void *vaddr, int32_t fd,
    size_t width, size_t height, uint32_t stride) {

  cl_int rc;
  uint32_t row_pitch = 0;

  assert(context_ != nullptr);

  cl_image_format format;
  format.image_channel_data_type = CL_UNSIGNED_INT8;
  format.image_channel_order = CL_RGBA;

  clGetDeviceImageInfoQCOM(device_id_, width, height, &format,
      CL_IMAGE_ROW_PITCH, sizeof(row_pitch), &row_pitch, NULL);
  if (stride < row_pitch) {
    OVDBG_ERROR("%s: Error stride: %d platform stride: %d",
      __func__, stride, row_pitch);
    return BAD_VALUE;
  }

  cl_mem_flags mem_flags = 0;
  mem_flags |= CL_MEM_READ_WRITE;
  mem_flags |= CL_MEM_USE_HOST_PTR;
  mem_flags |= CL_MEM_EXT_HOST_PTR_QCOM;

  cl_mem_ion_host_ptr ionmem{};
  ionmem.ext_host_ptr.allocation_type = CL_MEM_ION_HOST_PTR_QCOM;
  ionmem.ext_host_ptr.host_cache_policy = CL_MEM_HOST_WRITEBACK_QCOM;
  ionmem.ion_hostptr = vaddr;
  ionmem.ion_filedesc = fd;

  cl_image_desc desc;
  desc.image_type = CL_MEM_OBJECT_IMAGE2D;
  desc.image_width = width;
  desc.image_height = height;
  desc.image_depth = 0;
  desc.image_array_size = 0;
  desc.image_row_pitch = stride;
  desc.image_slice_pitch = desc.image_row_pitch * desc.image_height;
  desc.num_mip_levels = 0;
  desc.num_samples = 0;
  desc.buffer = nullptr;

  cl_buffer = clCreateImage( context_,
                             mem_flags,
                             &format,
                             &desc,
                             mem_flags & CL_MEM_EXT_HOST_PTR_QCOM ? &ionmem : nullptr,
                             &rc);
  if (CL_SUCCESS != rc) {
    OVDBG_ERROR("%s: Cannot create cl image memory object! rc %d", __func__, rc);
    return BAD_VALUE;
  }

  return 0;
}

int32_t OpenClKernel::unMapImage(cl_mem &cl_buffer) {
  return UnMapBuffer(cl_buffer);
}

int32_t OpenClKernel::SetKernelArgs(OpenClFrame &frame, OpenCLArgs &args) {

  OVDBG_VERBOSE("%s: Enter ", __func__);

  cl_uint arg_index = 0;/*  */
  cl_int cl_err;

  assert(context_ != nullptr);
  assert(command_queue_ != nullptr);

  cl_mem buf_to_process = frame.cl_buffer;
  cl_mem mask_to_process = args.mask;

  cl_uint offset_y = frame.plane0_offset + args.y * frame.stride0 + args.x;
  cl_uint offset_nv = frame.plane1_offset + args.y * frame.stride1 / 2 + args.x;
  cl_ushort swap_uv = frame.swap_uv;
  cl_ushort stride = frame.stride0;

  global_size_[0] = args.width / 2;
  global_size_[1] = args.height / 2;

  // __read_only image2d_t mask,   // 1
  cl_err =
      clSetKernelArg(kernel_, arg_index++, sizeof(cl_mem), &mask_to_process);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  // __global uchar *frame,        // 2
  cl_err =
      clSetKernelArg(kernel_, arg_index++, sizeof(cl_mem), &buf_to_process);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  // uint y_offset,                // 3
  cl_err = clSetKernelArg(kernel_, arg_index++, sizeof(cl_uint), &offset_y);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  // uint nv_offset,               // 4
  cl_err = clSetKernelArg(kernel_, arg_index++, sizeof(cl_uint), &offset_nv);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  // ushort stride,                // 5
  cl_err = clSetKernelArg(kernel_, arg_index++, sizeof(cl_ushort), &stride);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  // ushort swap_uv                // 6
  cl_err = clSetKernelArg(kernel_, arg_index++, sizeof(cl_ushort), &swap_uv);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to set Open cl kernel argument %d. rc: %d ",
        __func__, arg_index - 1, cl_err);
    return BAD_VALUE;
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

void OpenClKernel::ClCompleteCallback(cl_event event,
                                      cl_int event_command_exec_status,
                                      void *user_data) {

  OVDBG_VERBOSE("%s: Enter ", __func__);

  if (user_data != nullptr) {
    struct SyncObject *sync =  reinterpret_cast<struct SyncObject *>(user_data);
    std::unique_lock<std::mutex> lock(sync->lock_);
    sync->done_ = true;
    sync->signal_.Signal();
  }
  clReleaseEvent(event);

  OVDBG_VERBOSE("%s: Exit ", __func__);
}

int32_t OpenClKernel::RunCLKernel(bool wait_to_finish) {

  OVDBG_VERBOSE("%s: Enter ", __func__);

  cl_int cl_err = CL_SUCCESS;
  cl_event kernel_event = nullptr;

  assert(context_ != nullptr);
  assert(command_queue_ != nullptr);

  size_t *local_work_size =
      local_size_[0] + local_size_[1] == 0 ? nullptr : local_size_;

  cl_err = clEnqueueNDRangeKernel(
      command_queue_, kernel_, kernel_dimensions_, global_offset_, global_size_,
      local_work_size, 0, nullptr, wait_to_finish ? &kernel_event : nullptr);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to enqueue Open cl kernel! rc: %d ", __func__,
        cl_err);
    return BAD_VALUE;
  }

  if (wait_to_finish) {
    std::lock_guard<std::mutex> lock(sync_.lock_);
    sync_.done_ = false;
    cl_err = clSetEventCallback(kernel_event, CL_COMPLETE, &ClCompleteCallback,
        reinterpret_cast<void *>(&sync_));
    if (CL_SUCCESS != cl_err) {
      OVDBG_ERROR("%s: Failed to set Open cl kernel callback! rc: %d ",
          __func__, cl_err);
      return BAD_VALUE;
    }
  }

  if (wait_to_finish) {
    cl_err = clFlush(command_queue_);
    if (CL_SUCCESS != cl_err) {
      OVDBG_ERROR("%s: Failed to flush Open cl command queue! rc: %d ",
          __func__, cl_err);
      return BAD_VALUE;
    }
    std::chrono::nanoseconds wait_time(kWaitProcessTimeout);
    std::unique_lock<std::mutex> lock(sync_.lock_);
    while (sync_.done_ == false) {
      auto ret = sync_.signal_.WaitFor(lock, wait_time);
      if (ret != 0) {
        OVDBG_ERROR("%s: Timed out on Wait", __func__);
        return TIMED_OUT;
      }
    }
  }

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

std::string OpenClKernel::CreateCLKernelBuildLog() {

  cl_int cl_err;
  size_t log_size;
  cl_err = clGetProgramBuildInfo(prog_, device_id_, CL_PROGRAM_BUILD_LOG, 0,
                                 nullptr, &log_size);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to get Open cl build log size. rc: %d ", __func__,
        cl_err);
    return std::string();
  }

  std::string build_log;
  build_log.reserve(log_size);
  void *log = static_cast<void *>(const_cast<char *>(build_log.data()));
  cl_err = clGetProgramBuildInfo(prog_, device_id_, CL_PROGRAM_BUILD_LOG,
                                 log_size, log, nullptr);
  if (CL_SUCCESS != cl_err) {
    OVDBG_ERROR("%s: Failed to get Open cl build log. rc: %d ", __func__,
        cl_err);
    return std::string();
  }

  return build_log;
}

#endif // OVERLAY_OPEN_CL_BLIT

#ifdef OVERLAY_OPEN_CL_BLIT
Overlay::Overlay()
    : target_c2dsurface_id_(-1), blit_instance_(nullptr), ion_device_(-1),
     id_(0) {
}
#else // OVERLAY_OPEN_CL_BLIT
Overlay::Overlay()
    : target_c2dsurface_id_(-1), ion_device_(-1), id_(0) {
}
#endif // OVERLAY_OPEN_CL_BLIT

Overlay::~Overlay() {

  OVDBG_INFO("%s: Enter ",__func__);
  for (auto &iter : overlay_items_) {
    if (iter.second)
    delete iter.second;
  }
  overlay_items_.clear();

  if(target_c2dsurface_id_) {
    c2dDestroySurface(target_c2dsurface_id_);
    target_c2dsurface_id_ = 0;
    OVDBG_INFO("%s: Destroyed c2d Target Surface", __func__);
  }

  if (ion_device_ != -1) {
    ion_close(ion_device_);
    ion_device_ = -1;
  }

  OVDBG_INFO("%s: Exit ",__func__);
}

int32_t Overlay::Init(const TargetBufferFormat& format) {

  OVDBG_VERBOSE("%s:Enter", __func__);
  int32_t ret = 0;

  ion_device_ = ion_open();
  if (ion_device_ < 0) {
    OVDBG_ERROR("%s: Ion dev open failed %s\n", __func__, strerror(errno));
    return -1;
  }

#ifdef OVERLAY_OPEN_CL_BLIT
  blit_instance_ = OpenClKernel::New(BLIT_KERNEL, BLIT_KERNEL_NAME);
  if (ret) {
    OVDBG_ERROR("%s: Failed to build blit program", __func__);
    ion_close(ion_device_);
    ion_device_ = -1;
    return BAD_VALUE;
  }
#else // OVERLAY_OPEN_CL_BLIT
  uint32_t c2dColotFormat = GetC2dColorFormat(format);
  // Create dummy C2D surface, it is required to Initialize
  // C2D driver before calling any c2d Apis.
  C2D_YUV_SURFACE_DEF surface_def = {
    c2dColotFormat,
    1 * 4,
    1 * 4,
    (void*)0xaaaaaaaa,
    (void*)0xaaaaaaaa,
    1 * 4,
    (void*)0xaaaaaaaa,
    (void*)0xaaaaaaaa,
    1 * 4,
    (void*)0xaaaaaaaa,
    (void*)0xaaaaaaaa,
    1 * 4,
  };

  ret = c2dCreateSurface(&target_c2dsurface_id_, C2D_TARGET,
                         (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST |
                           C2D_SURFACE_WITH_PHYS |
                           C2D_SURFACE_WITH_PHYS_DUMMY),
                         &surface_def);
  if (ret != C2D_STATUS_OK) {
    ion_close(ion_device_);
    ion_device_ = -1;
    OVDBG_ERROR("%s: c2dCreateSurface failed!",__func__);
    return ret;
  }
#endif // OVERLAY_OPEN_CL_BLIT

  OVDBG_VERBOSE("%s: Exit",__func__);
  return ret;
}

int32_t Overlay::CreateOverlayItem(OverlayParam& param, uint32_t* overlay_id) {

  OVDBG_VERBOSE("%s:Enter ", __func__);
  OverlayItem* overlayItem = nullptr;

#ifdef OVERLAY_OPEN_CL_BLIT
  switch (param.type) {
    case OverlayType::kDateType:
      overlayItem = new OverlayItemDateAndTime(ion_device_, blit_instance_);
      break;
    case OverlayType::kUserText:
      overlayItem = new OverlayItemText(ion_device_, blit_instance_);
      break;
    case OverlayType::kStaticImage:
      overlayItem = new OverlayItemStaticImage(ion_device_, blit_instance_);
      break;
    case OverlayType::kBoundingBox:
      overlayItem = new OverlayItemBoundingBox(ion_device_, blit_instance_);
      break;
    case OverlayType::kPrivacyMask:
      overlayItem = new OverlayItemPrivacyMask(ion_device_, blit_instance_);
      break;
    case OverlayType::kGraph:
      overlayItem = new OverlayItemGraph(ion_device_, blit_instance_);
      break;
    default:
      OVDBG_ERROR("%s: OverlayType(%d) not supported!", __func__,
           param.type);
      break;
  }
#else // OVERLAY_OPEN_CL_BLIT
    switch (param.type) {
    case OverlayType::kDateType:
      overlayItem = new OverlayItemDateAndTime(ion_device_);
      break;
    case OverlayType::kUserText:
      overlayItem = new OverlayItemText(ion_device_);
      break;
    case OverlayType::kStaticImage:
      overlayItem = new OverlayItemStaticImage(ion_device_);
      break;
    case OverlayType::kBoundingBox:
      overlayItem = new OverlayItemBoundingBox(ion_device_);
      break;
    case OverlayType::kPrivacyMask:
      overlayItem = new OverlayItemPrivacyMask(ion_device_);
      break;
    case OverlayType::kGraph:
      overlayItem = new OverlayItemGraph(ion_device_);
      break;
    default:
      OVDBG_ERROR("%s: OverlayType(%d) not supported!", __func__,
           param.type);
      break;
  }
#endif // OVERLAY_OPEN_CL_BLIT

  if(!overlayItem) {
    OVDBG_ERROR("%s: OverlayItem type(%d) failed!", __func__, param.type);
    return NO_INIT;
  }

  auto ret = overlayItem->Init(param);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s:OverlayItem failed of type(%d)", __func__, param.type);
    delete overlayItem;
    return ret;
  }

  // StaticImage type overlayItem never be dirty as its contents are static,
  // all other items are dirty at Init time and will be marked as dirty whenever
  // their configuration changes at run time after first draw.
  if(param.type == OverlayType::kStaticImage) {
    overlayItem->MarkDirty(false);
  } else {
    overlayItem->MarkDirty(true);
  }

  *overlay_id = ++id_;
  overlay_items_.insert({*overlay_id, overlayItem});
  OVDBG_INFO("%s:OverlayItem Type(%d) Id(%d) Created Successfully !",__func__,
      param.type, *overlay_id);

  OVDBG_VERBOSE("%s:Exit ", __func__);
  return ret;
}

int32_t Overlay::DeleteOverlayItem(uint32_t overlay_id) {
  OVDBG_VERBOSE("%s:Enter ", __func__);
  std::lock_guard<std::mutex> lock(lock_);

  int32_t ret = 0;
  if(!IsOverlayItemValid(overlay_id)) {
      OVDBG_ERROR("%s: overlay_id(%d) is not valid!",__func__, overlay_id);
      return BAD_VALUE;
  }
  OverlayItem* overlayItem = overlay_items_.at(overlay_id);
  assert(overlayItem != nullptr);
  delete overlayItem;
  overlay_items_.erase(overlay_id);
  OVDBG_INFO("%s: overlay_id(%d) & overlayItem(0x%p) Removed from map",
      __func__, overlay_id, overlayItem);

  OVDBG_VERBOSE("%s:Exit ", __func__);
  return ret;
}

int32_t Overlay::GetOverlayParams(uint32_t overlay_id,
                                  OverlayParam& param) {
  int32_t ret = 0;
  if(!IsOverlayItemValid(overlay_id)) {
      OVDBG_ERROR("%s: overlay_id(%d) is not valid!",__func__, overlay_id);
      return BAD_VALUE;
  }
  OverlayItem* overlayItem = overlay_items_.at(overlay_id);
  assert(overlayItem != nullptr);

  memset(&param, 0x0, sizeof param);
  overlayItem->GetParameters(param);
  return ret;
}

int32_t Overlay::UpdateOverlayParams(uint32_t overlay_id,
                                     OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ", __func__);
  std::lock_guard<std::mutex> lock(lock_);

  if(!IsOverlayItemValid(overlay_id)) {
      OVDBG_ERROR("%s: overlay_id(%d) is not valid!",__func__, overlay_id);
      return BAD_VALUE;
  }
  OverlayItem* overlayItem = overlay_items_.at(overlay_id);
  assert(overlayItem != nullptr);

   OVDBG_VERBOSE("%s:Exit ", __func__);
  return overlayItem->UpdateParameters(param);
}


int32_t Overlay::EnableOverlayItem(uint32_t overlay_id) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  std::lock_guard<std::mutex> lock(lock_);

  int32_t ret = 0;
  if(!IsOverlayItemValid(overlay_id)) {
      OVDBG_ERROR("%s: overlay_id(%d) is not valid!",__func__, overlay_id);
      return BAD_VALUE;
  }
  OverlayItem* overlayItem = overlay_items_.at(overlay_id);
  assert(overlayItem != nullptr);

  overlayItem->Activate(true);
  OVDBG_DEBUG("%s: OverlayItem Id(%d) Activated", __func__, overlay_id);

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t Overlay::DisableOverlayItem(uint32_t overlay_id) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  std::lock_guard<std::mutex> lock(lock_);

  int32_t ret = 0;
  if(!IsOverlayItemValid(overlay_id)) {
      OVDBG_ERROR("%s: overlay_id(%d) is not valid!",__func__, overlay_id);
      return BAD_VALUE;
  }
  OverlayItem* overlayItem = overlay_items_.at(overlay_id);
  assert(overlayItem != nullptr);

  overlayItem->Activate(false);
  OVDBG_DEBUG("%s: OverlayItem Id(%d) DeActivated", __func__, overlay_id);

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

#ifdef OVERLAY_OPEN_CL_BLIT
int32_t Overlay::ApplyOverlay(const OverlayTargetBuffer& buffer) {

  OVDBG_VERBOSE("%s: Enter", __func__);
#ifdef DEBUG_BLIT_TIME
  auto start_time = ::std::chrono::high_resolution_clock::now();
#endif
  int32_t ret = 0;
  int32_t obj_idx = 0;

  std::lock_guard<std::mutex> lock(lock_);

  size_t numActiveOverlays = 0;
  bool isItemsActive = false;
  for (auto &iter : overlay_items_) {
    if ((iter).second->IsActive()) {
      isItemsActive = true;
    }
  }
  if (!isItemsActive) {
    OVDBG_VERBOSE("%s: No overlayItem is Active!", __func__);
    return ret;
  }
  assert(buffer.ion_fd != 0);
  assert(buffer.width != 0 && buffer.height != 0);
  assert(buffer.frame_len != 0);

  OVDBG_VERBOSE("%s:OverlayTargetBuffer: ion_fd = %d",__func__, buffer.ion_fd);
  OVDBG_VERBOSE("%s:OverlayTargetBuffer: Width = %d & Height = %d & frameLength"
      " =% d", __func__, buffer.width, buffer.height, buffer.frame_len);
  OVDBG_VERBOSE("%s: OverlayTargetBuffer: format = %d", __func__, buffer.format);

  void* bufVaddr = mmap(nullptr, buffer.frame_len, PROT_READ  | PROT_WRITE,
                                              MAP_SHARED, buffer.ion_fd, 0);
  if (!bufVaddr) {
    OVDBG_ERROR("%s: mmap failed!", __func__);
    return UNKNOWN_ERROR;
  }

  SyncStart(buffer.ion_fd);

  // map buffer
  OpenClFrame in_frame;
  ret = OpenClKernel::MapBuffer(in_frame.cl_buffer, bufVaddr, buffer.ion_fd,
      buffer.frame_len);
  if (ret) {
    OVDBG_ERROR("%s: Fail to map buffer to Open CL!", __func__);
    munmap(bufVaddr, buffer.frame_len);
    return UNKNOWN_ERROR;
  }

  // Iterate all dirty overlay Items, and update them.
  for (auto &iter : overlay_items_) {
    if ((iter).second->IsActive()) {
      ret = (iter).second->UpdateAndDraw();
      if (ret) {
        OVDBG_ERROR("%s: Update & Draw failed for Item=%d", __func__,
            (iter).first);
      }
    }
  }

  // Get config from overlay instances
  std::vector<DrawInfo> draw_infos;
  for (auto &iter : overlay_items_) {
    OverlayItem* overlay_item = (iter).second;
    if (overlay_item->IsActive()) {
      overlay_item->GetDrawInfo(buffer.width, buffer.height, draw_infos);
    }
  }

  in_frame.plane0_offset = 0;
  if (buffer.format == TargetBufferFormat::kYUVNV12) {
    in_frame.stride0 = VENUS_Y_STRIDE(COLOR_FMT_NV12, buffer.width);
    in_frame.stride1 = VENUS_UV_STRIDE(COLOR_FMT_NV12, buffer.width);
    in_frame.plane1_offset = in_frame.stride0 *
        VENUS_Y_SCANLINES(COLOR_FMT_NV12, buffer.height);
    in_frame.swap_uv = false;
  } else {
    in_frame.stride0 = VENUS_Y_STRIDE(COLOR_FMT_NV21, buffer.width);
    in_frame.stride1 = VENUS_UV_STRIDE(COLOR_FMT_NV21, buffer.width);
    in_frame.plane1_offset = in_frame.stride0 *
        VENUS_Y_SCANLINES(COLOR_FMT_NV21, buffer.height);
    in_frame.swap_uv = true;
  }

  // Configure kernels
  for (auto &item : draw_infos) {
    OpenCLArgs args;
    args.width  = item.width;
    args.height = item.height;
    args.x      = item.x;
    args.y      = item.y;
    args.mask   = item.mask;
    item.blit_inst->SetKernelArgs(in_frame, args);
  }

  // Apply kernels
  for (int i = 0; i < draw_infos.size(); i++) {
    draw_infos[i].blit_inst->RunCLKernel(i == draw_infos.size() - 1);
  }

  // unmap buffer
  OpenClKernel::UnMapBuffer(in_frame.cl_buffer);

EXIT:
  if (bufVaddr) {
    if (buffer.ion_fd)
      SyncEnd(buffer.ion_fd);

    munmap(bufVaddr, buffer.frame_len);
    bufVaddr = nullptr;
  }
#ifdef DEBUG_BLIT_TIME
  auto end_time = ::std::chrono::high_resolution_clock::now();
  auto diff = ::std::chrono::duration_cast<::std::chrono::milliseconds>
                  (end_time - start_time).count();
  OVDBG_INFO("%s: Time taken in 2D draw + Blit=%lld ms", __func__, diff);
#endif
  OVDBG_VERBOSE("%s: Exit ",__func__);
  return ret;
}

#else // OVERLAY_OPEN_CL_BLIT

int32_t Overlay::ApplyOverlay(const OverlayTargetBuffer& buffer) {

  OVDBG_VERBOSE("%s: Enter", __func__);

#ifdef DEBUG_BLIT_TIME
  auto start_time = ::std::chrono::high_resolution_clock::now();
#endif
  int32_t ret = 0;
  int32_t obj_idx = 0;

  std::lock_guard<std::mutex> lock(lock_);

  size_t numActiveOverlays = 0;
  bool isItemsActive = false;
  for (auto &iter : overlay_items_) {
    if ((iter).second->IsActive()) {
      isItemsActive = true;
    }
  }
  if(!isItemsActive) {
    OVDBG_VERBOSE("%s: No overlayItem is Active!", __func__);
    return ret;
  }
  assert(buffer.ion_fd != 0);
  assert(buffer.width != 0 && buffer.height != 0);
  assert(buffer.frame_len != 0);

  OVDBG_VERBOSE("%s:OverlayTargetBuffer: ion_fd = %d",__func__, buffer.ion_fd);
  OVDBG_VERBOSE("%s:OverlayTargetBuffer: Width = %d & Height = %d & frameLength"
      " =% d", __func__, buffer.width, buffer.height, buffer.frame_len);
  OVDBG_VERBOSE("%s: OverlayTargetBuffer: format = %d", __func__, buffer.format);

  void* bufVaddr = mmap(nullptr, buffer.frame_len, PROT_READ  | PROT_WRITE,
                                              MAP_SHARED, buffer.ion_fd, 0);
  if(!bufVaddr) {
    OVDBG_ERROR("%s: mmap failed!", __func__);
    return UNKNOWN_ERROR;
  }

  SyncStart(buffer.ion_fd);
  // Map input YUV buffer to GPU.
  void *gpuAddr = nullptr;
  ret = c2dMapAddr(buffer.ion_fd, bufVaddr, buffer.frame_len, 0,
                   KGSL_USER_MEM_TYPE_ION, &gpuAddr);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dMapAddr failed!",__func__);
    goto EXIT;
  }

  // Target surface format.
  C2D_YUV_SURFACE_DEF surface_def;
  surface_def.format  = GetC2dColorFormat(buffer.format);
  surface_def.width   = buffer.width;
  surface_def.height  = buffer.height;
  int32_t planeYLen;
  switch (surface_def.format) {
    case C2D_COLOR_FORMAT_420_NV12:
      //Y plane stride.
      surface_def.stride0 = VENUS_Y_STRIDE(COLOR_FMT_NV12,
              surface_def.width);

      //UV plane stride.
      surface_def.stride1 = VENUS_UV_STRIDE(COLOR_FMT_NV12,
              surface_def.width);

      //UV plane hostptr.
      planeYLen = surface_def.stride0 * VENUS_Y_SCANLINES(COLOR_FMT_NV12,
              surface_def.height);

      break;
    case C2D_COLOR_FORMAT_420_NV21:
      //Y plane stride.
      surface_def.stride0 = VENUS_Y_STRIDE(COLOR_FMT_NV21,
              surface_def.width);

      //UV plane stride.
      surface_def.stride1 = VENUS_UV_STRIDE(COLOR_FMT_NV21,
              surface_def.width);

      //UV plane hostptr.
      planeYLen = surface_def.stride0 * VENUS_Y_SCANLINES(COLOR_FMT_NV21,
              surface_def.height);

      break;
    case (C2D_COLOR_FORMAT_420_NV12 | C2D_FORMAT_UBWC_COMPRESSED):
      //Y plane stride.
      surface_def.stride0 = VENUS_Y_STRIDE(COLOR_FMT_NV12_UBWC,
              surface_def.width);

      //UV plane stride.
      surface_def.stride1 = VENUS_UV_STRIDE(COLOR_FMT_NV12_UBWC,
              surface_def.width);

      //UV plane hostptr.
      planeYLen = ROUND_TO(
                  VENUS_Y_META_STRIDE(COLOR_FMT_NV12_UBWC, surface_def.width) *
                  VENUS_Y_META_SCANLINES(COLOR_FMT_NV12_UBWC,
                  surface_def.height), 4096) +
                  ROUND_TO(surface_def.stride0 *
                  VENUS_Y_SCANLINES(COLOR_FMT_NV12_UBWC,
                  surface_def.height), 4096);
      break;
    default:
      OVDBG_ERROR("%s: Unknown format: %d", __func__, surface_def.format);
      goto EXIT;
  }

  OVDBG_DEBUG("%s: surface_def.stride0 = %d ",__func__, surface_def.stride0);
  OVDBG_DEBUG("%s: planeYLen = %d",__func__, planeYLen);

  //Y plane hostptr.
  surface_def.plane0  = (void*)bufVaddr;
  //Y plane Gpu address.
  surface_def.phys0   = (void*)gpuAddr;

  surface_def.plane1  = (void*)((intptr_t)bufVaddr + planeYLen);

  //UV plane Gpu address.
  surface_def.phys1 = (void*)((intptr_t)gpuAddr + planeYLen);

  //Create C2d target surface outof camera buffer. camera buffer
  //is target surface where c2d blits different types of overlays
  //static logo, system time and date.
  ret = c2dUpdateSurface(target_c2dsurface_id_, C2D_SOURCE,
                         (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST
                         |C2D_SURFACE_WITH_PHYS), &surface_def);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dUpdateSurface failed!",__func__);
    goto EXIT;
  }

  // Iterate all dirty overlay Items, and update them.
  for (auto &iter : overlay_items_) {
    if ((iter).second->IsActive()) {
      ret = (iter).second->UpdateAndDraw();
      if(ret != 0) {
        OVDBG_ERROR("%s: Update & Draw failed for Item=%d", __func__,
            (iter).first);
      }
    }
  }

  C2dObjects c2d_objects;
  memset(&c2d_objects, 0x0, sizeof c2d_objects);
  // Iterate all updated overlayItems, and get coordinates.
  for (auto &iter : overlay_items_) {
    std::vector<DrawInfo> draw_infos;
    OverlayItem* overlay_item = (iter).second;
    if (overlay_item->IsActive()) {
      overlay_item->GetDrawInfo(buffer.width, buffer.height, draw_infos);
      auto info_size = draw_infos.size();
      for (auto i = 0; i < info_size; i++) {
        c2d_objects.objects[obj_idx].surface_id = draw_infos[i].c2dSurfaceId;
        c2d_objects.objects[obj_idx].config_mask =
            C2D_ALPHA_BLEND_SRC_ATOP | C2D_TARGET_RECT_BIT;
      if (draw_infos[i].in_width) {
        c2d_objects.objects[obj_idx].config_mask        |= C2D_SOURCE_RECT_BIT;
        c2d_objects.objects[obj_idx].source_rect.x       = draw_infos[i].in_x << 16;
        c2d_objects.objects[obj_idx].source_rect.y       = draw_infos[i].in_y << 16;
        c2d_objects.objects[obj_idx].source_rect.width   =
            draw_infos[i].in_width << 16;
        c2d_objects.objects[obj_idx].source_rect.height  =
            draw_infos[i].in_height << 16;
      }
        c2d_objects.objects[obj_idx].target_rect.x = draw_infos[i].x << 16;
        c2d_objects.objects[obj_idx].target_rect.y = draw_infos[i].y << 16;
        c2d_objects.objects[obj_idx].target_rect.width =
            draw_infos[i].width << 16;
        c2d_objects.objects[obj_idx].target_rect.height =
            draw_infos[i].height << 16;

        OVDBG_VERBOSE("%s: c2d_objects[%d].surface_id=%d", __func__, obj_idx,
                      c2d_objects.objects[obj_idx].surface_id);
        OVDBG_VERBOSE("%s: c2d_objects[%d].target_rect.x=%d", __func__, obj_idx,
                      draw_infos[i].x);
        OVDBG_VERBOSE("%s: c2d_objects[%d].target_rect.y=%d", __func__, obj_idx,
                      draw_infos[i].y);
        OVDBG_VERBOSE("%s: c2d_objects[%d].target_rect.width=%d", __func__,
                      obj_idx, draw_infos[i].width);
        OVDBG_VERBOSE("%s: c2d_objects[%d].target_rect.height=%d", __func__,
                      obj_idx, draw_infos[i].height);
        ++numActiveOverlays;
        ++obj_idx;
      }
    }
  }

  OVDBG_VERBOSE("%s: numActiveOverlays=%d", __func__, numActiveOverlays);
  for(size_t i = 0; i < (numActiveOverlays-1); i++) {
    c2d_objects.objects[i].next = &c2d_objects.objects[i+1];
  }

  ret = c2dDraw(target_c2dsurface_id_, 0, 0, 0, 0, c2d_objects.objects,
                numActiveOverlays);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dDraw failed!",__func__);
    goto EXIT;
  }

  ret = c2dFinish(target_c2dsurface_id_);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dFinish failed!",__func__);
    goto EXIT;
  }
  // Unmap camera buffer from GPU after draw is completed.
  ret = c2dUnMapAddr(gpuAddr);
  if(ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dUnMapAddr failed!",__func__);
    goto EXIT;
  }

EXIT:
  if (bufVaddr) {
    if (buffer.ion_fd)
      SyncEnd(buffer.ion_fd);

    munmap(bufVaddr, buffer.frame_len);
    bufVaddr = nullptr;
  }
#ifdef DEBUG_BLIT_TIME
  auto end_time = ::std::chrono::high_resolution_clock::now();
  auto diff = ::std::chrono::duration_cast<::std::chrono::milliseconds>
                  (end_time - start_time).count();
  OVDBG_INFO("%s: Time taken in 2D draw + Blit=%lld ms", __func__, diff);
#endif
  OVDBG_VERBOSE("%s: Exit ",__func__);
  return ret;
}
#endif // OVERLAY_OPEN_CL_BLIT

int32_t Overlay::ProcessOverlayItems(
    const std::vector<OverlayParam>& overlay_list) {
  OVDBG_VERBOSE("%s: Enter", __func__);
  std::lock_guard<std::mutex> lock(lock_);

  int32_t ret = 0;
  uint32_t overlay_id = 0;
  uint32_t size = overlay_list.size();
  uint32_t num_items = overlay_items_.size();

  if (num_items < size) {
    auto overlay_param = overlay_list.at(0);
    for (auto i = 0; i < 10; i++) {
      ret = CreateOverlayItem(overlay_param, &overlay_id);
      if (ret) {
        OVDBG_ERROR("%s: CreateOverlayItem failed for id:%u!!", __func__,
                    overlay_id);
        return ret;
      }
    }
  }
  // Check overlay_items_ size and allocate in chunks of 10
  // If request size is greater than available allocate more
  // Remove active flag
  OVDBG_VERBOSE("%s: size:%u num_items:%u", __func__, size, num_items);
  auto items_iter = overlay_items_.begin();
  OverlayItem* overlayItem = nullptr;
  for (auto index = 0; index < size; index++, items_iter++) {
    auto overlay_param = overlay_list.at(index);
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;
    OVDBG_VERBOSE("%s:id:%u w: %u h:%u", __func__, overlay_id,
                  overlay_param.dst_rect.width,
                  overlay_param.dst_rect.height);
    ret = overlayItem->UpdateParameters(overlay_param);

    if (ret) {
      OVDBG_ERROR("%s: UpdateParameters failed for id: %u!", __func__,
                  overlay_id);
      return ret;
    }

    if (!overlayItem->IsActive()) {
      overlayItem->Activate(true);
      OVDBG_DEBUG("%s: OverlayItem Id(%d) Activated", __func__, overlay_id);
    } else {
      OVDBG_DEBUG("%s: OverlayItem Id(%d) already Activated", __func__,
                  overlay_id);
    }
  }
  // Disable inactive overlay
  while (items_iter != overlay_items_.end()) {
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;
    if (overlayItem->IsActive()) {
      OVDBG_DEBUG("%s: Disable overlayItem for id: %u!", __func__,
                    overlay_id);
      overlayItem->Activate(false);
    }
    items_iter++;
  }

  return ret;
  OVDBG_VERBOSE("%s: Exit", __func__);
}

int32_t Overlay::DeleteOverlayItems() {
  OVDBG_VERBOSE("%s: Enter", __func__);
  std::lock_guard<std::mutex> lock(lock_);
  int32_t ret = 0;
  uint32_t overlay_id = 0;
  OverlayItem* overlayItem = nullptr;

  auto items_iter = overlay_items_.begin();
  while (items_iter != overlay_items_.end()) {
    overlay_id = items_iter->first;
    overlayItem = items_iter->second;

    assert(overlayItem != nullptr);
    delete overlayItem;
    overlay_items_.erase(overlay_id);
    OVDBG_INFO("%s: overlay_id(%d) & overlayItem(0x%p) Removed from map",
               __func__, overlay_id, overlayItem);
    items_iter++;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

uint32_t Overlay::GetC2dColorFormat(const TargetBufferFormat& format) {

  uint32_t c2dColorFormat = C2D_COLOR_FORMAT_420_NV12;
  switch (format) {
    case TargetBufferFormat::kYUVNV12:
      c2dColorFormat = C2D_COLOR_FORMAT_420_NV12;
      break;
    case TargetBufferFormat::kYUVNV21:
      c2dColorFormat = C2D_COLOR_FORMAT_420_NV21;
      break;
    case TargetBufferFormat::kYUVNV12UBWC:
      c2dColorFormat = C2D_COLOR_FORMAT_420_NV12 | C2D_FORMAT_UBWC_COMPRESSED;
      break;
    default:
      OVDBG_ERROR("%s: Unsupported buffer format: %d", __func__, format);
      break;
  }

  OVDBG_VERBOSE("%s:Selected C2D ColorFormat=%d",__func__, c2dColorFormat);
  return c2dColorFormat;
}

bool Overlay::IsOverlayItemValid(uint32_t overlay_id) {

  OVDBG_DEBUG("%s: Enter overlay_id(%d)",__func__, overlay_id);
  bool valid = false;
  for (auto& iter : overlay_items_) {
    if (overlay_id == (iter).first) {
      valid = true;
      break;
    }
  }
  OVDBG_DEBUG("%s: Exit overlay_id(%d)",__func__, overlay_id);
  return valid;
}

#ifdef OVERLAY_OPEN_CL_BLIT
OverlayItem::OverlayItem(int32_t ion_device, OverlayType type,
    std::shared_ptr<OpenClKernel> &blit)
    : surface_(), location_type_(OverlayLocationType::kBottomLeft),
      dirty_(false), ion_device_(ion_device), type_(type), is_active_(false) {
  OVDBG_VERBOSE("%s:Enter ", __func__);

#if USE_CAIRO
  cr_surface_ = nullptr;
  cr_context_ = nullptr;
#endif

  if (blit.get()) {
    // Create local instance of blit kernel
    surface_.blit_inst_ = blit->AddInstance();
  }

  OVDBG_VERBOSE("%s:Exit ", __func__);
}
#else // OVERLAY_OPEN_CL_BLIT
OverlayItem::OverlayItem(int32_t ion_device, OverlayType type)
    : surface_(), location_type_(OverlayLocationType::kBottomLeft),
      dirty_(false), ion_device_(ion_device), type_(type), is_active_(false) {
  OVDBG_VERBOSE("%s:Enter ", __func__);

#if USE_CAIRO
  cr_surface_ = nullptr;
  cr_context_ = nullptr;
#endif

  OVDBG_VERBOSE("%s:Exit ", __func__);
}
#endif // OVERLAY_OPEN_CL_BLIT

OverlayItem::~OverlayItem() {
  DestroySurface();
}

void OverlayItem::MarkDirty(bool dirty) {
  dirty_ = dirty;
  OVDBG_VERBOSE("%s: OverlayItem Type(%d) marked dirty!", __func__, type_);
}

void OverlayItem::Activate(bool value) {
  is_active_ = value;
  OVDBG_VERBOSE("%s: OverlayItem Type(%d) Activated!", __func__, type_);
}

int32_t OverlayItem::AllocateIonMemory(IonMemInfo& mem_info, uint32_t size) {
  OVDBG_VERBOSE("%s:Enter", __func__);
  int32_t ret = 0;
  void* data = nullptr;
  uint32_t flags = ION_FLAG_CACHED;
  int32_t map_fd = -1;
  uint32_t heap_id_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
  size = ROUND_TO(size, 4096);

  ret = ion_alloc_fd(ion_device_, size, 0, heap_id_mask, flags, &map_fd);
  if (ret) {
    OVDBG_ERROR("%s:ION allocation failed\n", __func__);
    return -1;
  }

  data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
  if (data == MAP_FAILED) {
    OVDBG_ERROR("%s:ION mmap failed: %s (%d)\n", __func__, strerror(errno),
                errno);
    goto ION_MAP_FAILED;
  }
  SyncStart(map_fd);
  mem_info.fd = map_fd;
  mem_info.size = size;
  mem_info.vaddr = data;
  OVDBG_VERBOSE("%s:Exit ", __func__);
  return ret;

ION_MAP_FAILED:
  close(map_fd);
  return -1;
}

void OverlayItem::FreeIonMemory(void *&vaddr, int32_t &ion_fd, uint32_t size) {
  if (vaddr) {
    if (ion_fd != -1) SyncEnd(ion_fd);
    munmap(vaddr, size);
    vaddr = nullptr;
  }

  if (ion_fd != -1) {
    close(ion_fd);
    ion_fd = -1;
  }
}

int32_t OverlayItem::MapOverlaySurface(OverlaySurface &surface,
    IonMemInfo &mem_info, int32_t format) {

  OVDBG_VERBOSE("%s:Enter ", __func__);

  int32_t ret = 0;

#ifdef OVERLAY_OPEN_CL_BLIT
  ret = OpenClKernel::MapImage(surface.cl_buffer_, mem_info.vaddr,
      mem_info.fd, surface.width_, surface.height_, surface.width_ * 4);
  if (ret) {
    OVDBG_ERROR("%s: Failed to map image!",__func__);
    return -1;
  }

#else // OVERLAY_OPEN_CL_BLIT
  ret = c2dMapAddr(mem_info.fd, mem_info.vaddr, mem_info.size, 0,
                   KGSL_USER_MEM_TYPE_ION, &surface.gpu_addr_);
  if (ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dMapAddr failed!",__func__);
    return -1;
  }

  C2D_RGB_SURFACE_DEF c2dSurfaceDef;
  c2dSurfaceDef.format = format;
  c2dSurfaceDef.width  = surface.width_;
  c2dSurfaceDef.height = surface.height_;
  c2dSurfaceDef.buffer = mem_info.vaddr;
  c2dSurfaceDef.phys   = surface.gpu_addr_;
  c2dSurfaceDef.stride = surface.width_ * 4;

  // Create source c2d surface.
  ret = c2dCreateSurface(&surface.c2dsurface_id_, C2D_SOURCE,
                         (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST |
                          C2D_SURFACE_WITH_PHYS), &c2dSurfaceDef);
  if (ret != C2D_STATUS_OK) {
    OVDBG_ERROR("%s: c2dCreateSurface failed!",__func__);
    c2dUnMapAddr(surface.gpu_addr_);
    surface.gpu_addr_ = nullptr;
    return -1;
  }
#endif // OVERLAY_OPEN_CL_BLIT

  surface.ion_fd_ = mem_info.fd;
  surface.vaddr_  = mem_info.vaddr;
  surface.size_   = mem_info.size;

  OVDBG_VERBOSE("%s: Exit ", __func__);

  return 0;
}

void OverlayItem::UnMapOverlaySurface(OverlaySurface &surface) {

#ifdef OVERLAY_OPEN_CL_BLIT
  OpenClKernel::unMapImage(surface_.cl_buffer_);
#else // OVERLAY_OPEN_CL_BLIT
  if (surface.gpu_addr_) {
    c2dUnMapAddr(surface.gpu_addr_);
    surface.gpu_addr_ = nullptr;
    OVDBG_INFO("%s: Unmapped text GPU address for type(%d)", __func__, type_);
  }

  if (surface.c2dsurface_id_) {
    c2dDestroySurface(surface.c2dsurface_id_);
    surface.c2dsurface_id_ = -1;
    OVDBG_INFO("%s: Destroyed c2d text Surface for type(%d)", __func__, type_);
  }
#endif // OVERLAY_OPEN_CL_BLIT
}

void OverlayItem::ExtractColorValues(uint32_t hex_color, RGBAValues* color) {

  color->red   = ((hex_color >> 24) & 0xff) / 255.0;
  color->green = ((hex_color >> 16) & 0xff) / 255.0;
  color->blue  = ((hex_color >> 8) & 0xff) / 255.0;
  color->alpha = ((hex_color) & 0xff) / 255.0;
}

void OverlayItem::ClearSurface() {

#if USE_CAIRO
  RGBAValues bg_color;
  memset(&bg_color, 0x0, sizeof bg_color);
  // Painting entire surface with background color or with fully transparent
  // color doesn't work since cairo uses the OVER compositing operator
  // by default, and blending something entirely transparent OVER something
  // else has no effect at all until compositing operator is changed to SOURCE,
  // the SOURCE operator copies both color and alpha values directly from the
  // source to the destination instead of blending.
#ifdef DEBUG_BACKGROUND_SURFACE
  ExtractColorValues(BG_DEBUG_COLOR, &bg_color);
  cairo_set_source_rgba(cr_context_, bg_color.red, bg_color.green,
                        bg_color.blue, bg_color.alpha);
  cairo_set_operator(cr_context_, CAIRO_OPERATOR_SOURCE);
#else
  cairo_set_operator(cr_context_, CAIRO_OPERATOR_CLEAR);
#endif
  cairo_paint(cr_context_);
  cairo_surface_flush(cr_surface_);
  cairo_set_operator(cr_context_, CAIRO_OPERATOR_OVER);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));
  cairo_surface_mark_dirty(cr_surface_);
#endif
}

void OverlayItem::DestroySurface() {
  OVDBG_VERBOSE("%s: Enter", __func__);
  MarkDirty(true);
  UnMapOverlaySurface(surface_);
  FreeIonMemory(surface_.vaddr_, surface_.ion_fd_, surface_.size_);

#if USE_CAIRO
  if (cr_surface_) {
    cairo_surface_destroy(cr_surface_);
  }
  if (cr_context_) {
    cairo_destroy(cr_context_);
  }
#endif
  OVDBG_VERBOSE("%s: Exit", __func__);
}

OverlayItemStaticImage::~OverlayItemStaticImage() {
  OVDBG_VERBOSE("%s: Enter", __func__);
  image_path_.clear();
  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemStaticImage::DestroySurface() {
    OVDBG_VERBOSE("%s: Enter", __func__);
  MarkDirty(true);
  UnMapOverlaySurface(surface_);
  FreeIonMemory(surface_.vaddr_, surface_.ion_fd_, surface_.size_);
  OVDBG_VERBOSE("%s: Exit", __func__);
}

int32_t OverlayItemStaticImage::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t ret = 0;

  if(param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    OVDBG_ERROR("%s: Image Width & Height is not correct!", __func__);
    return BAD_VALUE;
  }

  location_type_ = param.location;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;
  width_         = param.dst_rect.width;
  height_        = param.dst_rect.height;
  image_type_    = param.image_info.image_type;

  if (param.image_info.image_type == OverlayImageType::kFilePath) {

    image_path_.setTo(param.image_info.image_location,
        strlen(param.image_info.image_location) + 1);
  } else if (param.image_info.image_type == OverlayImageType::kBlobType) {

    image_buffer_    = param.image_info.image_buffer;
    image_size_      = param.image_info.image_size;
    surface_.width_  = param.image_info.source_rect.width;
    surface_.height_ = param.image_info.source_rect.height;
    OVDBG_VERBOSE("%s: image blob  image_buffer_::0x%p  image_size_::%u "
        "image_width_::%u image_height_::%u ", __func__, image_buffer_,
            image_size_, surface_.width_, surface_.height_);

    char prop_val[PROPERTY_VALUE_MAX];
    property_get(PROP_DUMP_BLOB_IMAGE, prop_val, "0");
    blob_image_dump_enabled_ = (atoi(prop_val) == 0) ? false : true;

    if (blob_image_dump_enabled_) {
      FILE* pFile;
      pFile = fopen("/data/misc/qmmf/overlay_image_blob.rgb","wb");
      if (pFile ){
        fwrite(image_buffer_, sizeof(char), image_size_, pFile);
        fclose(pFile);
      }
    }

    crop_rect_x_      = param.image_info.source_rect.start_x;
    crop_rect_y_      = param.image_info.source_rect.start_y;
    crop_rect_width_  = param.image_info.source_rect.width;
    crop_rect_height_ = param.image_info.source_rect.height;
    OVDBG_VERBOSE("%s: image blob  crop_rect_x_::%u  crop_rect_y_::%u "
        "crop_rect_width_::%u  crop_rect_height_::%u",
        __func__, crop_rect_x_, crop_rect_y_,crop_rect_width_, crop_rect_height_);
  }

  ret = CreateSurface();
  if(ret != 0) {
    OVDBG_ERROR("%s: createLogoSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemStaticImage::UpdateAndDraw() {
#ifndef OVERLAY_OPEN_CL_BLIT
  // Nothing to update, contents are static.
  // Never marked as dirty.
  std::lock_guard<std::mutex> lock(update_param_lock_);
  if (blob_buffer_updated_) {
    c2dSurfaceUpdated(surface_.c2dsurface_id_, nullptr);
  }
#endif // OVERLAY_OPEN_CL_BLIT
  return OK;
}

void OverlayItemStaticImage::GetDrawInfo(uint32_t targetWidth,
                                         uint32_t targetHeight,
                                         std::vector<DrawInfo>& draw_infos) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  DrawInfo draw_info;
  memset(&draw_info, 0x0, sizeof(DrawInfo));
  draw_info.width  = width_;
  draw_info.height = height_;
  int32_t xMargin = targetWidth * OVERLAYITEM_X_MARGIN_PERCENT/100;
  int32_t yMargin = targetHeight * OVERLAYITEM_Y_MARGIN_PERCENT/100;
  int32_t x = 0;
  int32_t y = 0;

  switch (location_type_) {
    case OverlayLocationType::kTopLeft:
      x = xMargin;
      y = yMargin;
      break;
    case OverlayLocationType::kTopRight:
      x = targetWidth - (width_ + xMargin);
      y = yMargin;
      break;
    case OverlayLocationType::kCenter:
      x = (targetWidth - width_)/2;
      y = (targetHeight - height_)/2;
      break;
    case OverlayLocationType::kBottomLeft:
      x = xMargin;
      y = targetHeight - (height_ + yMargin);
      break;
    case OverlayLocationType::kBottomRight:
      x = targetWidth - (width_ + xMargin);
      y = targetHeight - (height_ + yMargin);
      break;
    case OverlayLocationType::kRandom:
      x = x_;
      y = y_;
      break;
    case OverlayLocationType::kNone:
    default:
      x = x_;
      y = y_;
      break;
  }
  draw_info.x            = x;
  draw_info.y            = y;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info.mask         = surface_.cl_buffer_;
  draw_info.blit_inst    = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT

  if (width_ != crop_rect_width_ || height_ != crop_rect_height_) {
    draw_info.in_width = crop_rect_width_;
    draw_info.in_height = crop_rect_height_;
    draw_info.in_x = crop_rect_x_;
    draw_info.in_y = crop_rect_y_;
  } else {
    draw_info.in_width = 0;
    draw_info.in_height = 0;
    draw_info.in_x = 0;
    draw_info.in_y = 0;
  }
  draw_infos.push_back(draw_info);

  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemStaticImage::GetParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type              = OverlayType::kStaticImage;
  param.location          = location_type_;
  param.dst_rect.start_x  = x_;
  param.dst_rect.start_y  = y_;
  param.dst_rect.width    = width_;
  param.dst_rect.height   = height_;
  std::string str(image_path_.string());
  str.copy(param.image_info.image_location, image_path_.length());
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

int32_t OverlayItemStaticImage::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  std::lock_guard<std::mutex> lock(update_param_lock_);
  int32_t ret = 0;

  if(strcmp(image_path_.string(), param.image_info.image_location) != 0) {
    OVDBG_ERROR("%s: Image Path Can't be changed at run time!!", __func__);
    return BAD_VALUE;
  }

  if(param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    OVDBG_ERROR("%s: Image Width & Height is not correct!", __func__);
    return BAD_VALUE;
  }

  location_type_ = param.location;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;
  width_         = param.dst_rect.width;
  height_        = param.dst_rect.height;

  if (image_type_ == OverlayImageType::kBlobType) {

    image_buffer_    = param.image_info.image_buffer;
    image_size_      = param.image_info.image_size;
    surface_.width_  = param.image_info.source_rect.width;
    surface_.height_ = param.image_info.source_rect.height;
    OVDBG_DEBUG("%s: updated image blob  image_buffer_::0x%p image_size_::%u "
        "image_width_::%u image_height_::%u ", __func__, image_buffer_,
        param.image_info.image_size, surface_.width_, surface_.height_);

    crop_rect_x_      = param.image_info.source_rect.start_x;
    crop_rect_y_      = param.image_info.source_rect.start_y;
    crop_rect_width_  = param.image_info.source_rect.width;
    crop_rect_height_ = param.image_info.source_rect.height;
    OVDBG_DEBUG("%s: updated image blob  crop_rect_x_::%u crop_rect_y_::%u "
        "crop_rect_width_::%u  crop_rect_height_::%u",
        __func__, crop_rect_x_, crop_rect_y_,crop_rect_width_, crop_rect_height_);

    if (blob_image_dump_enabled_) {
      String8 blobbuffer_filepath;
      struct timeval tv;
      gettimeofday(&tv, nullptr);

      blobbuffer_filepath.appendFormat("/data/misc/qmmf/overlay_blob_buffer_%lu.%s",
          tv.tv_sec, "rgb");

      blob_buffer_file_fd_ = open(blobbuffer_filepath.string(), O_CREAT |
          O_WRONLY | O_TRUNC, 0655);
      assert(blob_buffer_file_fd_ >= 0);

      uint32_t bytes_written;
      bytes_written  = write(blob_buffer_file_fd_, image_buffer_,
          param.image_info.image_size);

      if (bytes_written != param.image_info.image_size) {
        OVDBG_ERROR("Bytes written != %d and written = %u", bytes_written,
            param.image_info.image_size);
      }
      close(blob_buffer_file_fd_);
    }

    // only buffer content is changed not buffer size
    if (param.image_info.buffer_updated &&
        (param.image_info.image_size == image_size_)) {
      OVDBG_DEBUG("%s: updated image_size_:: %u param.image_info.image_size:: %u ",
          __func__, image_size_, param.image_info.image_size);
      uint32_t size = param.image_info.image_size;
      uint32_t* pixels = static_cast<uint32_t*>(surface_.vaddr_);
      memcpy(pixels, image_buffer_, size);
      blob_buffer_updated_ = param.image_info.buffer_updated;
      MarkDirty(true);
    } else if (param.image_info.image_size != image_size_) {

      image_size_ = param.image_info.image_size;
      DestroySurface();
      ret = CreateSurface();
      if (ret != 0) {
        OVDBG_ERROR("%s: CreateSurface failed!", __func__);
        return ret;
      }
    }
    image_size_= param.image_info.image_size;
  }

  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemStaticImage::CreateSurface() {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t   ret = 0;
  int32_t  format;
  uint32_t size;
  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));

  if (image_type_ == OverlayImageType::kFilePath)  {

    size = width_ * height_ * 4;
    ret = AllocateIonMemory(mem_info, size);
    if(0 != ret) {
      OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
      return ret;
    }
    uint32_t* pixels = (uint32_t*)mem_info.vaddr;

    //Load raw logo image file.
    FILE *file = 0;
    size_t bytes;

    file = fopen(image_path_.string(), "rb");
    if(file) {
      bytes = fread(pixels, 1, size, file);
      OVDBG_INFO("%s: Total btyes = %d",__func__,bytes);
      if(bytes != size) {
        OVDBG_ERROR("%s: Raw file format is not correct",__func__);
        fclose(file);
        goto ERROR;
      }
      fclose(file);
    } else {
      OVDBG_ERROR("%s: (%s)File open Failed!!",__func__, image_path_.string());
      goto ERROR;
    }
  } else if(image_type_ == OverlayImageType::kBlobType){

    size = image_size_;
    ret = AllocateIonMemory(mem_info, size);
    if(0 != ret) {
      OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
      return ret;
    }
    uint32_t* pixels = static_cast<uint32_t*>(mem_info.vaddr);
    memcpy(pixels, image_buffer_, size);
  }

  format = C2D_FORMAT_SWAP_ENDIANNESS | C2D_COLOR_FORMAT_8888_RGBA;
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

  OVDBG_VERBOSE("%s: Exit ",__func__);
  return ret;
ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

#ifdef OVERLAY_OPEN_CL_BLIT
OverlayItemDateAndTime::OverlayItemDateAndTime(int32_t ion_device,
    std::shared_ptr<OpenClKernel> &blit)
        : OverlayItem(ion_device, OverlayType::kDateType, blit) {
  OVDBG_VERBOSE("%s:Enter ", __func__);
  memset(&date_time_type_, 0x0, sizeof date_time_type_);
  date_time_type_.time_format = OverlayTimeFormatType::kHHMM_24HR;
  date_time_type_.date_format = OverlayDateFormatType::kMMDDYYYY;
  OVDBG_VERBOSE("%s:Exit", __func__);
}
#else // OVERLAY_OPEN_CL_BLIT
OverlayItemDateAndTime::OverlayItemDateAndTime(int32_t ion_device)
        : OverlayItem(ion_device, OverlayType::kDateType) {
  OVDBG_VERBOSE("%s:Enter ", __func__);
  memset(&date_time_type_, 0x0, sizeof date_time_type_);
  date_time_type_.time_format = OverlayTimeFormatType::kHHMM_24HR;
  date_time_type_.date_format = OverlayDateFormatType::kMMDDYYYY;
  OVDBG_VERBOSE("%s:Exit", __func__);
}
#endif // OVERLAY_OPEN_CL_BLIT

OverlayItemDateAndTime::~OverlayItemDateAndTime() {
  OVDBG_VERBOSE("%s:Enter ", __func__);
  OVDBG_VERBOSE("%s:Exit ", __func__);
}

int32_t OverlayItemDateAndTime::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  location_type_ = param.location;
  text_color_    = param.color;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;
  width_         = param.dst_rect.width;
  height_        = param.dst_rect.height;
  prev_time_     = 0;

  date_time_type_.date_format = param.date_time.date_format;
  date_time_type_.time_format = param.date_time.time_format;

  // Create surface with the same aspect ratio
  surface_.width_ = ROUND_TO(kCairoBufferMinWidth, 16);
  surface_.height_ = kCairoBufferMinWidth * height_ / width_;

  // Recalculate if surface height is less than minimum
  if (surface_.height_ < kCairoBufferMinHeight) {
    surface_.height_ = kCairoBufferMinHeight;
    surface_.width_ = ROUND_TO(kCairoBufferMinHeight * width_ / height_, 16);
    // recalculated height according to aligned width
    surface_.height_ = surface_.width_ * height_ / width_;
  }

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface();
  if(ret != 0) {
    OVDBG_ERROR("%s: createLogoSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemDateAndTime::UpdateAndDraw() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t ret = 0;
  if(!dirty_)
      return ret;

  struct timeval tv;
  time_t now_time;
  struct tm *time;
  char date_buf[40];
  char time_buf[40];

  gettimeofday(&tv, nullptr);
  now_time = tv.tv_sec;
  OVDBG_VERBOSE("%s: curr time %ld prev time %ld", __func__,
      now_time, prev_time_);

  if (prev_time_ == now_time) {
     MarkDirty(true);
     return ret;
  }
  prev_time_ = now_time;
  time = localtime(&now_time);

  switch(date_time_type_.date_format) {
    case OverlayDateFormatType::kYYYYMMDD:
      strftime(date_buf, sizeof date_buf, "%Y/%m/%d", time);
      break;
    case OverlayDateFormatType::kMMDDYYYY:
    default:
      strftime(date_buf, sizeof date_buf, "%m/%d/%Y", time);
      break;
  }
  switch(date_time_type_.time_format) {
    case OverlayTimeFormatType::kHHMMSS_24HR:
      strftime(time_buf, sizeof time_buf, "%H:%M:%S", time);
      break;
    case OverlayTimeFormatType::kHHMMSS_AMPM:
      strftime(time_buf, sizeof time_buf, "%r", time);
      break;
    case OverlayTimeFormatType::kHHMM_24HR:
      strftime(time_buf, sizeof time_buf, "%H:%M", time);
      break;
    case OverlayTimeFormatType::kHHMM_AMPM:
    default:
      strftime(time_buf, sizeof time_buf, "%I:%M %p", time);
      break;
  }
  OVDBG_VERBOSE("%s: date:time (%s:%s)", __func__, date_buf, time_buf);

  double x_date, x_time, y_date, y_time;
  x_date = x_time = y_date = y_time = 0.0;

  SyncStart(surface_.ion_fd_);
#if USE_CAIRO
  // Clear the privous drawn contents.
  ClearSurface();
  cairo_select_font_face(cr_context_, "@cairo:Georgia", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr_context_, kTextSize);
  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  cairo_font_extents_t font_extent;
  cairo_font_extents (cr_context_, &font_extent);
  OVDBG_VERBOSE("%s: ascent=%f, descent=%f, height=%f, max_x_advance=%f,"
      " max_y_advance = %f", __func__, font_extent.ascent, font_extent.descent,
       font_extent.height, font_extent.max_x_advance,
       font_extent.max_y_advance);

  cairo_text_extents_t date_text_extents;
  cairo_text_extents (cr_context_, date_buf, &date_text_extents);

  OVDBG_VERBOSE("%s: Date: te.x_bearing=%f, te.y_bearing=%f, te.width=%f,"
      " te.height=%f, te.x_advance=%f, te.y_advance=%f", __func__,
      date_text_extents.x_bearing, date_text_extents.y_bearing,
      date_text_extents.width, date_text_extents.height,
      date_text_extents.x_advance, date_text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_DEFAULT);
  cairo_set_font_options (cr_context_, options);
  cairo_font_options_destroy (options);

  //(0,0) is at topleft corner of draw buffer.
  x_date = (surface_.width_ - date_text_extents.width) / 2.0;
  y_date = std::max(surface_.height_ / 2.0, date_text_extents.height);
  OVDBG_VERBOSE("%s: x_date=%f, y_date=%f, ref=%f", __func__, x_date, y_date,
      date_text_extents.height - (font_extent.descent/2.0));
  cairo_move_to (cr_context_, x_date, y_date);

  // Draw date.
  RGBAValues text_color;
  memset(&text_color, 0x0, sizeof text_color);
  ExtractColorValues(text_color_, &text_color);
  cairo_set_source_rgba (cr_context_, text_color.red, text_color.green,
                         text_color.blue, text_color.alpha);

  cairo_show_text (cr_context_, date_buf);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  // Draw time.
  cairo_text_extents_t time_text_extents;
  cairo_text_extents (cr_context_, time_buf, &time_text_extents);
  OVDBG_VERBOSE("%s: Time: te.x_bearing=%f, te.y_bearing=%f, te.width=%f,"
      " te.height=%f, te.x_advance=%f, te.y_advance=%f", __func__,
      time_text_extents.x_bearing, time_text_extents.y_bearing,
      time_text_extents.width, time_text_extents.height,
      time_text_extents.x_advance, time_text_extents.y_advance);
  // Calculate the x_time to draw the time text extact middle of buffer.
  // Use x_width which usually few pixel less than the width of the actual
  // drawn text.
  x_time = (surface_.width_ - time_text_extents.width) / 2.0;
  y_time = y_date + date_text_extents.height;
  cairo_move_to (cr_context_, x_time, y_time);
  cairo_show_text (cr_context_, time_buf);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  cairo_surface_flush(cr_surface_);
  cairo_surface_mark_dirty(cr_surface_);
#elif USE_SKIA

#ifndef DEBUG_BACKGROUND_SURFACE
  canvas_->clear(SK_AlphaOPAQUE);
#else
  canvas_->clear(SK_ColorDKGRAY);
#endif

  const char* delm = " : ";
  std::string data_time_buf;
  data_time_buf += date_buf;
  data_time_buf += delm;
  data_time_buf += time_buf;

  SkPaint paint;
  paint.setColor(text_color_);
  paint.setTextSize(SkIntToScalar(kTextSize));
  paint.setAntiAlias(false);
  paint.setTextScaleX(1);

  SkString dateText(data_time_buf.c_str(), data_time_buf.size());
  y_date = DATETIME_TEXT_BUF_HEIGHT - kTextSize;
  canvas_->drawText(dateText.c_str(), dateText.size(), x_date, y_date, paint);
  canvas_->flush();
#endif
  SyncEnd(surface_.ion_fd_);
  MarkDirty(true);
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

void OverlayItemDateAndTime::GetDrawInfo(uint32_t targetWidth,
                                         uint32_t targetHeight,
                                         std::vector<DrawInfo>& draw_infos) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  DrawInfo draw_info;
  memset(&draw_info, 0x0, sizeof(DrawInfo));

  draw_info.width  = width_;
  draw_info.height = height_;

  int32_t xMargin = targetWidth * OVERLAYITEM_X_MARGIN_PERCENT/100;
  int32_t yMargin = targetHeight * OVERLAYITEM_Y_MARGIN_PERCENT/100;
  int32_t x = 0;
  int32_t y = 0;

  //(0,0) is at topleft corner.
  switch (location_type_) {
    case OverlayLocationType::kTopLeft:
      x = xMargin;
      y = yMargin;
      break;
    case OverlayLocationType::kTopRight:
      x = targetWidth - (draw_info.width + xMargin);
      y = yMargin;
      break;
    case OverlayLocationType::kCenter:
      x = (targetWidth - draw_info.width)/2;
      y = (targetHeight - draw_info.height)/2;
      break;
    case OverlayLocationType::kBottomLeft:
      x = xMargin;
      y = targetHeight - (draw_info.height + yMargin);
      break;
    case OverlayLocationType::kBottomRight:
      x = targetWidth - (draw_info.width + xMargin);
      y = targetHeight - (draw_info.height + yMargin);
      break;
    case OverlayLocationType::kRandom:
      x = x_;
      y = y_;
      break;
    case OverlayLocationType::kNone:
    default:
      x = x_;
      y = y_;
      break;
  }
  draw_info.x            = x;
  draw_info.y            = y;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info.mask         = surface_.cl_buffer_;
  draw_info.blit_inst    = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info);
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

void OverlayItemDateAndTime::GetParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type             = OverlayType::kDateType;
  param.location         = location_type_;
  param.color            = text_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width   = width_;
  param.dst_rect.height  = height_;
  param.date_time.date_format = date_time_type_.date_format;
  param.date_time.time_format = date_time_type_.time_format;
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

int32_t OverlayItemDateAndTime::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t ret = 0;

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  location_type_ = param.location;
  text_color_    = param.color;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;

  date_time_type_.date_format = param.date_time.date_format;
  date_time_type_.time_format = param.date_time.time_format;

  if (width_ != param.dst_rect.width || height_ != param.dst_rect.height) {
    width_ = param.dst_rect.width;
    height_ = param.dst_rect.height;
    prev_time_ = 0;

    // Create surface with the same aspect ratio
    surface_.width_ = ROUND_TO(kCairoBufferMinWidth, 16);
    surface_.height_ = kCairoBufferMinWidth * height_ / width_;

    // Recalculate if surface height is less than minimum
    if (surface_.height_ < kCairoBufferMinHeight) {
      surface_.height_ = kCairoBufferMinHeight;
      surface_.width_ = ROUND_TO(kCairoBufferMinHeight * width_ / height_, 16);
      // recalculated height according to aligned width
      surface_.height_ = surface_.width_ * height_ / width_;
    }

    OVDBG_INFO("%s: New Offscreen buffer:(%dx%d)",__func__, surface_.width_,
        surface_.height_);

    DestroySurface();
    ret = CreateSurface();
    if (ret != 0) {
      OVDBG_ERROR("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemDateAndTime::CreateSurface() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t ret = 0;
  int32_t format;
  int32_t size = width_ * height_ * 4;
  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));

  ret = AllocateIonMemory(mem_info, size);
  if(0 != ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
    return ret;
  }
  OVDBG_INFO("%s: ION memory allocated fd = %d",__func__,mem_info.fd);

#if USE_CAIRO
  cr_surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>
                                                    (mem_info.vaddr),
                                                    CAIRO_FORMAT_ARGB32,
                                                    surface_.width_,
                                                    surface_.height_,
                                                    surface_.width_ * 4);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

#elif USE_SKIA
  //Create Skia canvas outof ION memory.
  SkImageInfo imageInfo = SkImageInfo::Make(width_, height_,
      kRGBA_8888_SkColorType, kPremul_SkAlphaType);

#ifdef ANDROID_O_OR_ABOVE
  canvas_ = (SkCanvas::MakeRasterDirect(imageInfo, mem_info.vaddr,
                                      width_ *4)).release();
#else
  canvas_ = SkCanvas::NewRasterDirect(imageInfo, mem_info.vaddr,
                                      width_ *4);
#endif
  if(!canvas_) {
    OVDBG_ERROR("%s: Skia Creation failed!!",__func__);
    goto ERROR;
  }
#endif
  //Draw system time on Skia canvas.
  UpdateAndDraw();

#if USE_CAIRO
  format = C2D_COLOR_FORMAT_8888_ARGB;
#elif USE_SKIA
  format = C2D_FORMAT_SWAP_ENDIANNESS | C2D_COLOR_FORMAT_8888_RGBA;
#endif
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

#ifdef OVERLAY_OPEN_CL_BLIT
OverlayItemBoundingBox::OverlayItemBoundingBox(int32_t ion_device,
                         std::shared_ptr<OpenClKernel> &blit)
                    : OverlayItem(ion_device, OverlayType::kBoundingBox, blit),
                      bbox_name_(), text_height_(0) {
  OVDBG_VERBOSE("%s: Enter", __func__);
  if (blit.get()) {
    // Create local instance of blit kernel
    text_surface_.blit_inst_ = blit->AddInstance();
  }
  OVDBG_VERBOSE("%s: Exit", __func__);
};
#else // OVERLAY_OPEN_CL_BLIT
OverlayItemBoundingBox::OverlayItemBoundingBox(int32_t ion_device)
                    : OverlayItem(ion_device, OverlayType::kBoundingBox),
                      bbox_name_(), text_height_(0) {
  OVDBG_VERBOSE("%s: Enter", __func__);
  OVDBG_VERBOSE("%s: Exit", __func__);
};
#endif // OVERLAY_OPEN_CL_BLIT



OverlayItemBoundingBox::~OverlayItemBoundingBox() {
  OVDBG_INFO("%s: Enter", __func__);
  DestroyTextSurface();
  OVDBG_INFO("%s: Exit", __func__);
}

int32_t OverlayItemBoundingBox::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  if ((param.dst_rect.width <= 0) || (param.dst_rect.height <= 0)) {
    return BAD_VALUE;
  }
  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  x_          = param.dst_rect.start_x;
  y_          = param.dst_rect.start_y;
  width_      = param.dst_rect.width;
  height_     = param.dst_rect.height;
  bbox_color_ = param.color;

  surface_.width_  = kBoxBuffWidth;
  surface_.height_ = ROUND_TO((surface_.width_ * height_) / width_, 2);

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
      surface_.height_);

#if USE_CAIRO
  text_surface_.width_ = 320;
  text_surface_.height_ = 80;
  box_stroke_width_ = (kStrokeWidth * surface_.width_ + width_ - 1) / width_;

  char prop_val[PROPERTY_VALUE_MAX];
  property_get(PROP_BOX_STROKE_WIDTH, prop_val, "4");
  box_stroke_width_ = (static_cast<uint32_t>(atoi(prop_val)) >
      box_stroke_width_) ? static_cast<uint32_t>(atoi(prop_val)) :
      box_stroke_width_;
#endif

  int32_t textLen = strlen(param.bounding_box.box_name);

  int32_t textLimit = std::min(textLen + 1, kTextLimit);
  bbox_name_.setTo(param.bounding_box.box_name, textLimit);

  auto ret = CreateSurface();
  if (ret != 0) {
    OVDBG_ERROR("%s: CreateSurface failed!", __func__);
    return NO_INIT;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemBoundingBox::UpdateAndDraw() {

  OVDBG_VERBOSE("%s: Enter ", __func__);
  int32_t ret = 0;

  if(!dirty_) {
    OVDBG_DEBUG("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }
  //  First text is drawn.
  //  ----------
  //  | TEXT   |
  //  ----------
  // Then bounding box is drawn
  //  ----------
  //  |        |
  //  |  BOX   |
  //  |        |
  //  ----------


  SyncStart(surface_.ion_fd_);
  SyncStart(text_surface_.ion_fd_);
#if USE_CAIRO
  OVDBG_INFO("%s: Draw bounding box and text!", __func__);
  ClearSurface();
  ClearTextSurface();
  // Draw text first.
  cairo_select_font_face(text_cr_context_, "@cairo:Georgia", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_BOLD);

  cairo_set_font_size (text_cr_context_, kTextSize);
  cairo_set_antialias(text_cr_context_, CAIRO_ANTIALIAS_BEST);

  cairo_font_extents_t font_extents;
  cairo_font_extents (text_cr_context_, &font_extents);
  OVDBG_VERBOSE("%s: BBox Font: ascent=%f, descent=%f, height=%f, "
      "max_x_advance=%f, max_y_advance = %f", __func__, font_extents.ascent,
      font_extents.descent, font_extents.height, font_extents.max_x_advance,
      font_extents.max_y_advance);

  cairo_text_extents_t text_extents;
  cairo_text_extents (text_cr_context_, bbox_name_.string(), &text_extents);

  OVDBG_VERBOSE("%s: BBox Text: te.x_bearing=%f, te.y_bearing=%f, te.width=%f,"
      " te.height=%f, te.x_advance=%f, te.y_advance=%f", __func__,
      text_extents.x_bearing, text_extents.y_bearing,
      text_extents.width, text_extents.height,
      text_extents.x_advance, text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_BEST);
  cairo_set_font_options (text_cr_context_, options);
  cairo_font_options_destroy (options);

  double x_text = 0.0;
  double y_text = text_extents.height + (font_extents.descent/2.0);
  OVDBG_VERBOSE("%s: x_text=%f, y_text=%f", __func__, x_text, y_text);
  cairo_move_to (text_cr_context_, x_text, y_text);

  RGBAValues bbox_color;
  memset(&bbox_color, 0x0, sizeof bbox_color);
  ExtractColorValues(bbox_color_, &bbox_color);
  cairo_set_source_rgba (text_cr_context_, bbox_color.red, bbox_color.green,
                         bbox_color.blue, bbox_color.alpha);
  cairo_show_text (text_cr_context_, bbox_name_.string());
  assert(CAIRO_STATUS_SUCCESS == cairo_status(text_cr_context_));
  cairo_surface_flush (text_cr_surface_);

  // Draw rectangle
  cairo_set_line_width (cr_context_, box_stroke_width_);
  cairo_set_source_rgba (cr_context_, bbox_color.red, bbox_color.green,
                         bbox_color.blue, bbox_color.alpha);
  cairo_rectangle (cr_context_, box_stroke_width_ / 2, box_stroke_width_ / 2,
                   surface_.width_ - box_stroke_width_,
                   surface_.height_ - box_stroke_width_);
  cairo_stroke (cr_context_);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  cairo_surface_flush (cr_surface_);
#elif USE_SKIA
  if (width_ > 0 && height_ > 0) {

#ifndef DEBUG_BACKGROUND_SURFACE
  canvas_->clear(SK_AlphaOPAQUE);
#else
  canvas_->clear(SK_ColorDKGRAY);
#endif
    SkPaint paintBox, paintText;
    paintText.setColor(bbox_color_);
    paintBox.setColor(bbox_color_);

    paintText.setTextSize(SkIntToScalar(kTextSize));
    paintText.setAntiAlias(true);

    paintBox.setStrokeWidth(box_stroke_width);
    paintBox.setStyle(SkPaint::kStroke_Style);

    int32_t xText = 0, yText = 0;
    int32_t xBBox = 0, yBBox = 0;
    if(bbox_name_.length() > 1) {
      SkString text(bbox_name_.string(), bbox_name_.length());
      // Text size is always 20% of buffer height.
      yText  = surface_.height_ * kTextPercent / 100;
      // Margin between text and bouding box rect.
      yText  = yText - kTextMargin;
      canvas_->drawText(text.c_str(), text.size(), xText, yText, paintText);
    }
    yBBox = yText > 0 ? kTextSize : 0;
    int32_t boxWidth  = kBoxBuffWidth;
    int32_t boxHeight = surface_.height_ - yBBox;
    text_surface_.height_ = yText;
    canvas_->drawRect(SkRect::MakeXYWH(xBBox, yBBox, boxWidth, boxHeight),
                      paintBox);
    canvas_->flush();
  }
#endif
  SyncEnd(surface_.ion_fd_);
  SyncEnd(text_surface_.ion_fd_);
  MarkDirty(false);
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

void OverlayItemBoundingBox::GetDrawInfo(uint32_t targetWidth,
                                         uint32_t targetHeight,
                                         std::vector<DrawInfo>& draw_infos) {
  OVDBG_VERBOSE("%s: Enter", __func__);
  DrawInfo draw_info_bbox;
  memset(&draw_info_bbox, 0x0, sizeof(DrawInfo));
  draw_info_bbox.x = x_;
  draw_info_bbox.y = y_;
  draw_info_bbox.width = width_;
  draw_info_bbox.height = height_;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info_bbox.mask = surface_.cl_buffer_;
  draw_info_bbox.blit_inst = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info_bbox.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info_bbox);
#if USE_CAIRO
  DrawInfo draw_info_text;
  memset(&draw_info_text, 0x0, sizeof(DrawInfo));
  draw_info_text.x = x_ + kTextMargin;
  draw_info_text.y = y_ + kTextMargin;
  draw_info_text.width = (targetWidth * kTextPercent) / 100;
  draw_info_text.height =
      (draw_info_text.width * text_surface_.height_) / text_surface_.width_;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info_text.mask = text_surface_.cl_buffer_;
  draw_info_text.blit_inst  = text_surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info_text.c2dSurfaceId = text_surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info_text);
#endif
  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemBoundingBox::GetParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type             = OverlayType::kBoundingBox;
  param.location         = OverlayLocationType::kNone;
  param.color            = bbox_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width   = width_;
  param.dst_rect.height  = height_;
  std::string str(bbox_name_.string());
  str.copy(param.bounding_box.box_name, bbox_name_.length());
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

void OverlayItemBoundingBox::ClearTextSurface() {
#if USE_CAIRO
  RGBAValues bg_color;
  memset(&bg_color, 0x0, sizeof bg_color);
  // Painting entire surface with background color or with fully transparent
  // color doesn't work since cairo uses the OVER compositing operator
  // by default, and blending something entirely transparent OVER something
  // else has no effect at all until compositing operator is changed to SOURCE,
  // the SOURCE operator copies both color and alpha values directly from the
  // source to the destination instead of blending.
#ifdef DEBUG_BACKGROUND_SURFACE
  ExtractColorValues(BG_DEBUG_COLOR, &bg_color);
  cairo_set_source_rgba(text_cr_context_, bg_color.red, bg_color.green,
                        bg_color.blue, bg_color.alpha);
  cairo_set_operator(text_cr_context_, CAIRO_OPERATOR_SOURCE);
#else
  cairo_set_operator(text_cr_context_, CAIRO_OPERATOR_CLEAR);
#endif
  cairo_paint(text_cr_context_);
  cairo_surface_flush(text_cr_surface_);
  cairo_set_operator(text_cr_context_, CAIRO_OPERATOR_OVER);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(text_cr_context_));
  cairo_surface_mark_dirty(text_cr_surface_);
#endif
}

int32_t OverlayItemBoundingBox::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t ret = 0;

  if((param.dst_rect.width <= 0) || (param.dst_rect.height <= 0)) {
      return BAD_VALUE;
  }
  if(param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
      return BAD_VALUE;
  }
  x_          = param.dst_rect.start_x;
  y_          = param.dst_rect.start_y;
  width_      = param.dst_rect.width;
  height_     = param.dst_rect.height;

  if (surface_.height_ != ROUND_TO((surface_.width_ * height_) / width_, 2)) {
    surface_.height_ = ROUND_TO((surface_.width_ * height_) / width_, 2);
    DestroySurface();
    DestroyTextSurface();
    ret = CreateSurface();
    if (ret != 0) {
      OVDBG_ERROR("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

#if USE_CAIRO
  if (box_stroke_width_ !=
      (kStrokeWidth * surface_.width_ + width_ - 1) / width_) {
    box_stroke_width_ = (kStrokeWidth * surface_.width_ + width_ - 1) / width_;
    MarkDirty(true);
  }
#endif

  if (bbox_color_ != param.color) {
    bbox_color_ = param.color;
    MarkDirty(true);
  }

  if (strcmp(bbox_name_.string(), param.bounding_box.box_name)) {
    bbox_name_.clear();
    int32_t textLen = strlen(param.bounding_box.box_name);
    int32_t textLimit = std::min(textLen + 1, kTextLimit);
    bbox_name_.setTo(param.bounding_box.box_name, textLimit);
    MarkDirty(true);
  }

  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemBoundingBox::CreateSurface() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t size = surface_.width_ * surface_.height_ * 4;
  int32_t format;

  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory(mem_info, size);
  if(0 != ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
    return ret;
  }
  OVDBG_DEBUG("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

#if USE_CAIRO
  cr_surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>
                                                    (mem_info.vaddr),
                                                    CAIRO_FORMAT_ARGB32,
                                                    surface_.width_,
                                                    surface_.height_,
                                                    surface_.width_ * 4);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

#elif USE_SKIA
  //Create Skia canvas outof ION memory.
  SkImageInfo imageInfo = SkImageInfo::Make(kBoxBuffWidth,
      surface_.height_, kRGBA_8888_SkColorType, kPremul_SkAlphaType);

#ifdef ANDROID_O_OR_ABOVE
  canvas_ = (SkCanvas::MakeRasterDirect(imageInfo, mem_info.vaddr,
                                      kBoxBuffWidth *4)).release();
#else
  canvas_ = SkCanvas::NewRasterDirect(imageInfo, mem_info.vaddr,
                                      kBoxBuffWidth *4);
#endif
  if(!canvas_) {
    OVDBG_ERROR("%s: Skia Creation failed!!", __func__);
    goto ERROR;
  }
#endif

#if USE_CAIRO
  format = C2D_COLOR_FORMAT_8888_ARGB;
#elif USE_SKIA
  format = C2D_FORMAT_SWAP_ENDIANNESS | C2D_COLOR_FORMAT_8888_RGBA;
#endif
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

#if USE_CAIRO
  // Setup text surface
  size = text_surface_.width_ * text_surface_.height_ * 4;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));
  ret = AllocateIonMemory(mem_info, size);
  if (ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed", __func__);
    return ret;
  }
  OVDBG_INFO("%s: Ion memory allocated fd = %d", __func__, mem_info.fd);

  text_cr_surface_ = cairo_image_surface_create_for_data(
      static_cast<unsigned char*>(mem_info.vaddr), CAIRO_FORMAT_ARGB32,
      text_surface_.width_, text_surface_.height_, text_surface_.width_ * 4);
  assert(text_cr_surface_ != nullptr);
  text_cr_context_ = cairo_create(text_cr_surface_);
  assert(text_cr_context_ != nullptr);

  format = C2D_COLOR_FORMAT_8888_ARGB;
  ret = MapOverlaySurface(text_surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

#endif

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
#if USE_CAIRO
  close(text_surface_.ion_fd_);
  text_surface_.ion_fd_ = -1;
#endif
  return ret;
}

void OverlayItemBoundingBox::DestroyTextSurface() {
  bbox_name_.clear();
#if USE_CAIRO
  UnMapOverlaySurface(text_surface_);
  FreeIonMemory(text_surface_.vaddr_, text_surface_.ion_fd_,
    text_surface_.size_);

  if (text_cr_surface_) {
    cairo_surface_destroy(text_cr_surface_);
  }
  if (text_cr_context_) {
    cairo_destroy(text_cr_context_);
  }
#endif
}

OverlayItemText::~OverlayItemText() {
  OVDBG_VERBOSE("%s:Enter ", __func__);
  OVDBG_VERBOSE("%s:Exit ", __func__);
}

int32_t OverlayItemText::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  location_type_ = param.location;
  text_color_    = param.color;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;
  width_         = param.dst_rect.width;
  height_        = param.dst_rect.height;
  text_          = param.user_text;

  surface_.width_ = std::max(kCairoBufferMinWidth, width_);
  surface_.width_ = ROUND_TO(surface_.width_, 16);
  surface_.height_ = std::max(kCairoBufferMinHeight, height_);

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface();
  if(ret != 0) {
    OVDBG_ERROR("%s: CreateSurface failed!", __func__);
    return ret;
  }
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemText::UpdateAndDraw() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t ret = 0;

  if(!dirty_)
    return ret;

  SyncStart(surface_.ion_fd_);

  // Split the Text based on new line character.
  vector < string > res;
  stringstream ss(text_); // Turn the string into a stream.
  string tok;
  while (getline(ss, tok, '\n')) {
    OVDBG_INFO("%s: UserText:: Substring: %s", __func__, tok.c_str());
    res.push_back(tok);
  }

#if USE_CAIRO
  ClearSurface();
  cairo_select_font_face(cr_context_, "@cairo:Georgia", CAIRO_FONT_SLANT_NORMAL,
                          CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size (cr_context_, kTextSize);
  cairo_set_antialias (cr_context_, CAIRO_ANTIALIAS_BEST);
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  cairo_font_extents_t font_extent;
  cairo_font_extents (cr_context_, &font_extent);
  OVDBG_VERBOSE("%s: ascent=%f, descent=%f, height=%f, max_x_advance=%f,"
      " max_y_advance = %f", __func__, font_extent.ascent, font_extent.descent,
       font_extent.height, font_extent.max_x_advance,
       font_extent.max_y_advance);

  cairo_text_extents_t text_extents;
  cairo_text_extents (cr_context_, text_.c_str(), &text_extents);

  OVDBG_VERBOSE("%s: Custom text: te.x_bearing=%f, te.y_bearing=%f,"
      " te.width=%f, te.height=%f, te.x_advance=%f, te.y_advance=%f", __func__,
      text_extents.x_bearing, text_extents.y_bearing,
      text_extents.width, text_extents.height,
      text_extents.x_advance, text_extents.y_advance);

  cairo_font_options_t *options;
  options = cairo_font_options_create ();
  cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_DEFAULT);
  cairo_set_font_options (cr_context_, options);
  cairo_font_options_destroy (options);

  //(0,0) is at topleft corner of draw buffer.
  double x_text = 0.0;
  double y_text = 0.0;

  // Draw Text.
  RGBAValues text_color;
  memset(&text_color, 0x0, sizeof text_color);
  ExtractColorValues(text_color_, &text_color);
  cairo_set_source_rgba (cr_context_, text_color.red, text_color.green,
                         text_color.blue, text_color.alpha);
  for (string substr: res) {
    y_text += text_extents.height + (font_extent.descent/2.0);
    OVDBG_VERBOSE("%s: x_text=%f, y_text=%f", __func__, x_text, y_text);
    cairo_move_to (cr_context_, x_text, y_text);
    cairo_show_text (cr_context_, substr.c_str());
    assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));
  }
  cairo_surface_flush(cr_surface_);

#elif USE_SKIA

#ifndef DEBUG_BACKGROUND_SURFACE
  canvas_->clear(SK_AlphaOPAQUE);
#else
  canvas_->clear(SK_ColorDKGRAY);
#endif

  SkPaint paint;
  paint.setColor(text_color_);
  paint.setTextSize(SkIntToScalar(kTextSize));
  paint.setAntiAlias(true);

  int32_t x = 0;
  int32_t y = 0;
  for (string substr: res) {
    // This op is required to maintain proper gap between 2 lines.
    y += paint.getTextSize() * 1.2f;
    SkString skText(substr.c_str(), substr.length());
    canvas_->drawText(skText.c_str(), skText.size(), x, y, paint);
  }
  canvas_->flush();
#endif
  SyncEnd(surface_.ion_fd_);
  dirty_ = false;
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

void OverlayItemText::GetDrawInfo(uint32_t targetWidth,
                                  uint32_t targetHeight,
                                  std::vector<DrawInfo>& draw_infos) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  DrawInfo draw_info;
  memset(&draw_info, 0x0, sizeof(DrawInfo));

  draw_info.width  = width_;
  draw_info.height = height_;

  int32_t xMargin = targetWidth * OVERLAYITEM_X_MARGIN_PERCENT/100;
  int32_t yMargin = targetHeight * OVERLAYITEM_Y_MARGIN_PERCENT/100;
  int32_t x = 0;
  int32_t y = 0;

  // (0,0) is at topleft corner.
  switch (location_type_) {
    case OverlayLocationType::kTopLeft:
      x = xMargin;
      y = yMargin;
      break;
    case OverlayLocationType::kTopRight:
      x = targetWidth - (draw_info.width + xMargin);
      y = yMargin;
      break;
    case OverlayLocationType::kCenter:
      x = (targetWidth - draw_info.width)/2;
      y = (targetHeight - draw_info.height)/2;
      break;
    case OverlayLocationType::kBottomLeft:
      x = xMargin;
      y = targetHeight - (draw_info.height + yMargin);
      break;
    case OverlayLocationType::kBottomRight:
      x = targetWidth - (draw_info.width + xMargin);
      y = targetHeight - (draw_info.height + yMargin);
      break;
    case OverlayLocationType::kRandom:
      x = x_;
      y = y_;
      break;
    case OverlayLocationType::kNone:
    default:
      x = x_;
      y = y_;
      break;
  }
  draw_info.x            = x;
  draw_info.y            = y;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info.mask         = surface_.cl_buffer_;
  draw_info.blit_inst    = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info);

  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemText::GetParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type             = OverlayType::kUserText;
  param.location         = location_type_;
  param.color            = text_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width   = width_;
  param.dst_rect.height  = height_;
  int size = std::min(text_.length(), sizeof(param.user_text) - 1);
  text_.copy(param.user_text, size);
  param.user_text[size + 1] = '\0';
  OVDBG_VERBOSE("%s:Exit ", __func__);
}

int32_t OverlayItemText::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t ret = 0;

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  location_type_ = param.location;
  x_             = param.dst_rect.start_x;
  y_             = param.dst_rect.start_y;

  if (width_ != param.dst_rect.width || height_ != param.dst_rect.height) {
    width_ = param.dst_rect.width;
    height_ = param.dst_rect.height;

    surface_.width_ = std::max(kCairoBufferMinWidth, width_);
    surface_.width_ = ROUND_TO(surface_.width_, 16);
    surface_.height_ = std::max(kCairoBufferMinHeight, height_);

    OVDBG_INFO("%s: New Offscreen buffer:(%dx%d)",__func__, surface_.width_,
        surface_.height_);

    DestroySurface();
    ret = CreateSurface();
    if (ret != 0) {
      OVDBG_ERROR("%s: CreateSurface failed!", __func__);
      return ret;
    }
  }

  if (text_color_ != param.color) {
    text_color_ = param.color;
    MarkDirty(true);
  }

  if (text_.compare(param.user_text)) {
    text_ = param.user_text;
    MarkDirty(true);
  }

  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemText::CreateSurface() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t size = width_ * height_ * 4;
  int32_t format;
  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));

  auto ret = AllocateIonMemory(mem_info, size);
  if(0 != ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
    return ret;
  }
  OVDBG_INFO("%s: Ion memory allocated fd = %d", __func__, mem_info.fd);
#if USE_CAIRO
  cr_surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>
                                                    (mem_info.vaddr),
                                                    CAIRO_FORMAT_ARGB32,
                                                    surface_.width_,
                                                    surface_.height_,
                                                    surface_.width_ * 4);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);

#elif USE_SKIA
  //Create Skia canvas outof ION memory.
  SkImageInfo imageInfo = SkImageInfo::Make(width_, height_,
      kRGBA_8888_SkColorType, kPremul_SkAlphaType);

#ifdef ANDROID_O_OR_ABOVE
  canvas_ = (SkCanvas::MakeRasterDirect(imageInfo, mem_info.vaddr,
                                      width_ * 4)).release();
#else
  canvas_ = SkCanvas::NewRasterDirect(imageInfo, mem_info.vaddr,
                                      width_ * 4);
#endif
  if(!canvas_) {
    OVDBG_ERROR("%s: Skia Creation failed!!",__func__);
    goto ERROR;
  }
#endif
  //Draw system time on Skia canvas.
  UpdateAndDraw();

#if USE_CAIRO
  format = C2D_COLOR_FORMAT_8888_ARGB;
#elif USE_SKIA
  format = C2D_FORMAT_SWAP_ENDIANNESS | C2D_COLOR_FORMAT_8888_RGBA;
#endif
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

  OVDBG_INFO("%s: Exit", __func__);
  return ret;

ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

int32_t OverlayItemPrivacyMask::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);

  if((param.dst_rect.width <= 0) || (param.dst_rect.height <= 0)) {
    return BAD_VALUE;
  }
  if(param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }

  x_          = param.dst_rect.start_x;
  y_          = param.dst_rect.start_y;
  width_      = param.dst_rect.width;
  height_     = param.dst_rect.height;
  mask_color_ = param.color;
  config_     = param.privacy_mask;

  surface_.width_  = std::min(width_, kMaskBoxBufWidth);
  surface_.height_ = (surface_.width_ * height_) / width_;
  surface_.height_ = ROUND_TO(surface_.height_, 2);

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
    surface_.height_);

  auto ret = CreateSurface();
  if(ret != 0) {
    OVDBG_ERROR("%s: CreateSurface failed!", __func__);
    return NO_INIT;
  }
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemPrivacyMask::UpdateAndDraw() {

  OVDBG_VERBOSE("%s: Enter ", __func__);
  int32_t ret = 0;

  if(!dirty_) {
    OVDBG_DEBUG("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }
  SyncStart(surface_.ion_fd_);
#if USE_CAIRO
  ClearSurface();
  RGBAValues mask_color;
  ExtractColorValues(mask_color_, &mask_color);
  cairo_set_source_rgba(cr_context_, mask_color.red, mask_color.green,
                        mask_color.blue, mask_color.alpha);
  switch (config_.type) {
    case OverlayPrivacyMaskType::kRectangle: {
      uint32_t x = (config_.rectangle.start_x * surface_.width_) / width_;
      uint32_t y = (config_.rectangle.start_y * surface_.width_) / width_;
      uint32_t w = (config_.rectangle.width * surface_.width_) / width_;
      uint32_t h = (config_.rectangle.height * surface_.width_) / width_;
      cairo_rectangle(cr_context_, x, y, w, h);
      cairo_fill(cr_context_);
    }
      break;

    case OverlayPrivacyMaskType::kInverseRectangle: {
      uint32_t x = (config_.rectangle.start_x * surface_.width_) / width_;
      uint32_t y = (config_.rectangle.start_y * surface_.width_) / width_;
      uint32_t w = (config_.rectangle.width * surface_.width_) / width_;
      uint32_t h = (config_.rectangle.height * surface_.width_) / width_;
      cairo_rectangle(cr_context_, 0, 0, surface_.width_, surface_.height_);
      cairo_rectangle(cr_context_, x, y, w, h);
      cairo_set_fill_rule(cr_context_, CAIRO_FILL_RULE_EVEN_ODD);
      cairo_fill(cr_context_);
    }
      break;

    case OverlayPrivacyMaskType::kCircle: {
      uint32_t cx = (config_.circle.center_x * surface_.width_) / width_;
      uint32_t cy = (config_.circle.center_y * surface_.height_) / height_;
      uint32_t rad = (config_.circle.radius * surface_.width_) / width_;
      cairo_arc(cr_context_, cx, cy, rad, 0, 2 * M_PI);
      cairo_fill(cr_context_);
    }
      break;

    case OverlayPrivacyMaskType::kInverseCircle: {
      uint32_t cx = (config_.circle.center_x * surface_.width_) / width_;
      uint32_t cy = (config_.circle.center_y * surface_.height_) / height_;
      uint32_t rad = (config_.circle.radius * surface_.width_) / width_;
      cairo_arc(cr_context_, cx, cy, rad, 0, 2 * M_PI);
      cairo_rectangle(cr_context_, 0, 0, surface_.width_, surface_.height_);
      cairo_set_fill_rule(cr_context_, CAIRO_FILL_RULE_EVEN_ODD);
      cairo_fill(cr_context_);
    }
      break;

    default:
      OVDBG_DEBUG("%s: Unsupported privacy mask type %d", __func__,
          config_.type);
      return -1;
  }
  assert(CAIRO_STATUS_SUCCESS == cairo_status(cr_context_));

  cairo_surface_flush (cr_surface_);
#elif USE_SKIA
  //Create Skia canvas outof ION memory.
  SkPaint paintBox;

#ifndef DEBUG_BACKGROUND_SURFACE
  canvas_->clear(SK_AlphaOPAQUE);
#else
  canvas_->clear(SK_ColorDKGRAY);
#endif

  paintBox.setColor(mask_color_);
  paintBox.setStyle(SkPaint::kFill_Style);
  //For blurring effect
#ifdef ANDROID_O_OR_ABOVE
  paintBox.setMaskFilter(SkBlurMaskFilter::Make(kNormal_SkBlurStyle,5.0f, 0));
#else
  paintBox.setMaskFilter(SkBlurMaskFilter::Create(kNormal_SkBlurStyle,5.0f, 0));
#endif
  OVDBG_VERBOSE("x %d y %d width %d height %d", x_, y_, width_, height_);
  canvas_->drawRect(SkRect::MakeXYWH(0, 0, width_, height_),
      paintBox);
  canvas_->flush();
#endif
  SyncEnd(surface_.ion_fd_);
  // Don't paint until params gets updated by app(UpdateParameters).
  MarkDirty(false);
  return OK;
}

void OverlayItemPrivacyMask::GetDrawInfo(uint32_t targetWidth,
                                         uint32_t targetHeight,
                                         std::vector<DrawInfo>& draw_infos) {

  OVDBG_VERBOSE("%s: Enter", __func__);
  DrawInfo draw_info;
  memset(&draw_info, 0x0, sizeof(DrawInfo));
  draw_info.x            = x_;
  draw_info.y            = y_;
  draw_info.width        = width_;
  draw_info.height       = height_;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info.mask         = surface_.cl_buffer_;
  draw_info.blit_inst    = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info);
  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemPrivacyMask::GetParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type             = OverlayType::kPrivacyMask;
  param.location         = OverlayLocationType::kNone;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width   = width_;
  param.dst_rect.height  = height_;
  param.color            = mask_color_;
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

int32_t OverlayItemPrivacyMask::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t ret = 0;

  if((param.dst_rect.width <= 0) || (param.dst_rect.height <= 0)) {
    return BAD_VALUE;
  }
  if(param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    return BAD_VALUE;
  }
  x_          = param.dst_rect.start_x;
  y_          = param.dst_rect.start_y;
  width_      = param.dst_rect.width;
  height_     = param.dst_rect.height;
  mask_color_ = param.color;
  config_     = param.privacy_mask;

  surface_.width_  = kMaskBoxBufWidth;
  surface_.height_ = (surface_.width_ * height_) / width_;
  surface_.height_ = ROUND_TO(surface_.height_, 2);

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
    surface_.height_);


  // Mark dirty, updated contents would be re-painted in next paint cycle.
  MarkDirty(true);
  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemPrivacyMask::CreateSurface() {

  OVDBG_VERBOSE("%s: Enter", __func__);

  int32_t size = surface_.width_ * surface_.height_ * 4;
  int32_t format;
  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));

  auto ret = AllocateIonMemory(mem_info, size);
  if(0 != ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
    return ret;
  }
  OVDBG_DEBUG("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);
#if USE_CAIRO
  cr_surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>
                                                    (mem_info.vaddr),
                                                    CAIRO_FORMAT_ARGB32,
                                                    surface_.width_,
                                                    surface_.height_,
                                                    surface_.width_ * 4);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);
#elif USE_SKIA
  //Create Skia canvas outof ION memory.
  SkImageInfo imageInfo = SkImageInfo::Make(surface_.width_,
      surface_.heght_, kRGBA_8888_SkColorType, kPremul_SkAlphaType);

#ifdef ANDROID_O_OR_ABOVE
  canvas_ = (SkCanvas::MakeRasterDirect(imageInfo, mem_info.vaddr,
                                      surface_.width_ *4)).release();
#else
  canvas_ = SkCanvas::NewRasterDirect(imageInfo, mem_info.vaddr,
                                      surface_.width_ *4);
#endif
  if(!canvas_) {
    OVDBG_ERROR("%s: Skia Creation failed!!", __func__);
    goto ERROR;
  }
#endif

  format = C2D_COLOR_FORMAT_8888_ARGB;
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;

ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

int32_t OverlayItemGraph::Init(OverlayParam& param) {

  OVDBG_VERBOSE("%s: Enter", __func__);

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    OVDBG_ERROR("%s: failed: dim: %dx%d", __func__,
      param.dst_rect.width, param.dst_rect.height);
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    OVDBG_ERROR("%s: failed: x/y: %dx%d", __func__,
      param.dst_rect.start_x, param.dst_rect.start_y);
    return BAD_VALUE;
  }

  if (param.graph.points_count > OVERLAY_GRAPH_NODES_MAX_COUNT) {
    OVDBG_ERROR("%s: failed: points_count %d", __func__,
        param.graph.points_count);
    return BAD_VALUE;
  }

  if (param.graph.chain_count > OVERLAY_GRAPH_CHAIN_MAX_COUNT) {
    OVDBG_ERROR("%s: failed: chain_count %d", __func__,
        param.graph.chain_count);
    return BAD_VALUE;
  }

  x_           = param.dst_rect.start_x;
  y_           = param.dst_rect.start_y;
  width_       = param.dst_rect.width;
  height_      = param.dst_rect.height;
  graph_color_ = param.color;
  graph_       = param.graph;

  float scaled_width  = static_cast<float>(width_) / DOWNSCALE_FACTOR;
  float scaled_height = static_cast<float>(height_) / DOWNSCALE_FACTOR;

  float aspect_ratio = scaled_width / scaled_height;

  OVDBG_INFO("%s: Graph(W:%dxH:%d), aspect_ratio(%f), scaled(W:%fxH:%f)",
      __func__, param.dst_rect.width, param.dst_rect.height,
      aspect_ratio, scaled_width, scaled_height);

  int32_t width = static_cast<int32_t>(round(scaled_width));
  width = ROUND_TO(width, 16); // Round to multiple of 16.
  width = width > kGraphBufWidth ? width : kGraphBufWidth;
  int32_t height = (static_cast<int32_t>(width/aspect_ratio + 15)>> 4) << 4;
  height = height > kGraphBufHeight ? height : kGraphBufHeight;

  surface_.width_  = width;
  surface_.height_ = height;

  downscale_ratio_ = (float)width_ / (float)surface_.width_;

  OVDBG_INFO("%s: Offscreen buffer:(%dx%d)",__func__, surface_.width_,
      surface_.height_);

  auto ret = CreateSurface();
  if (ret != 0) {
    OVDBG_ERROR("%s: CreateSurface failed!", __func__);
    return NO_INIT;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

int32_t OverlayItemGraph::UpdateAndDraw() {

  OVDBG_VERBOSE("%s: Enter ", __func__);
  int32_t ret = 0;

  if(!dirty_) {
    OVDBG_DEBUG("%s: Item is not dirty! Don't draw!", __func__);
    return ret;
  }

  SyncStart(surface_.ion_fd_);
#if USE_CAIRO
  OVDBG_INFO("%s: Draw graph!", __func__);
  ClearSurface();

  RGBAValues bbox_color;
  memset(&bbox_color, 0x0, sizeof bbox_color);
  ExtractColorValues(graph_color_, &bbox_color);
  cairo_set_source_rgba (cr_context_, bbox_color.red, bbox_color.green,
                         bbox_color.blue, bbox_color.alpha);
  cairo_set_line_width (cr_context_, kLineWidth);

  // draw key points
  for (int i = 0; i < graph_.points_count; i++) {
    if (graph_.points[i].x >= 0 && graph_.points[i].y >= 0) {
      cairo_arc (cr_context_,
        (uint32_t)((float) graph_.points[i].x / downscale_ratio_),
        (uint32_t)((float) graph_.points[i].y / downscale_ratio_),
        kDotRadius, 0, 2 * M_PI);
      cairo_fill (cr_context_);
    }
  }

  // draw links
  for (int i = 0; i < graph_.chain_count; i++) {
    cairo_move_to (cr_context_,
      (uint32_t)((float) graph_.points[graph_.chain[i][0]].x / downscale_ratio_),
      (uint32_t)((float) graph_.points[graph_.chain[i][0]].y / downscale_ratio_));
    cairo_line_to (cr_context_,
      (uint32_t)((float) graph_.points[graph_.chain[i][1]].x / downscale_ratio_),
      (uint32_t)((float) graph_.points[graph_.chain[i][1]].y / downscale_ratio_));
    cairo_stroke (cr_context_);
  }

  cairo_surface_flush (cr_surface_);
#endif
  SyncEnd(surface_.ion_fd_);

  MarkDirty(false);
  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
}

void OverlayItemGraph::GetDrawInfo(uint32_t targetWidth,
                                         uint32_t targetHeight,
                                         std::vector<DrawInfo>& draw_infos) {
  OVDBG_VERBOSE("%s: Enter", __func__);
  DrawInfo draw_info;
  memset(&draw_info, 0x0, sizeof(DrawInfo));
  draw_info.x = x_;
  draw_info.y = y_;
  draw_info.width = width_;
  draw_info.height = height_;
#ifdef OVERLAY_OPEN_CL_BLIT
  draw_info.mask = surface_.cl_buffer_;
  draw_info.blit_inst = surface_.blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  draw_info.c2dSurfaceId = surface_.c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
  draw_infos.push_back(draw_info);
  OVDBG_VERBOSE("%s: Exit", __func__);
}

void OverlayItemGraph::GetParameters(OverlayParam& param) {
  OVDBG_VERBOSE("%s:Enter ",__func__);
  param.type             = OverlayType::kGraph;
  param.location         = OverlayLocationType::kNone;
  param.color            = graph_color_;
  param.dst_rect.start_x = x_;
  param.dst_rect.start_y = y_;
  param.dst_rect.width   = width_;
  param.dst_rect.height  = height_;
  OVDBG_VERBOSE("%s:Exit ",__func__);
}

int32_t OverlayItemGraph::UpdateParameters(OverlayParam& param) {

  OVDBG_VERBOSE("%s:Enter ",__func__);
  int32_t ret = 0;

  if (param.dst_rect.width <= 0 || param.dst_rect.height <= 0) {
    OVDBG_ERROR("%s: failed: dim: %dx%d", __func__,
      param.dst_rect.width, param.dst_rect.height);
    return BAD_VALUE;
  }

  if (param.dst_rect.start_x < 0 || param.dst_rect.start_y < 0) {
    OVDBG_ERROR("%s: failed: x/y: %dx%d", __func__,
      param.dst_rect.start_x, param.dst_rect.start_y);
    return BAD_VALUE;
  }

  if (param.graph.points_count > OVERLAY_GRAPH_NODES_MAX_COUNT) {
    OVDBG_ERROR("%s: failed: points_count %d", __func__,
        param.graph.points_count);
    return BAD_VALUE;
  }

  if (param.graph.chain_count > OVERLAY_GRAPH_CHAIN_MAX_COUNT) {
    OVDBG_ERROR("%s: failed: chain_count %d", __func__,
        param.graph.chain_count);
    return BAD_VALUE;
  }

  x_           = param.dst_rect.start_x;
  y_           = param.dst_rect.start_y;
  width_       = param.dst_rect.width;
  height_      = param.dst_rect.height;
  graph_color_ = param.color;
  graph_       = param.graph;
  MarkDirty(true);

  OVDBG_VERBOSE("%s:Exit ",__func__);
  return ret;
}

int32_t OverlayItemGraph::CreateSurface() {

  OVDBG_VERBOSE("%s: Enter", __func__);
  int32_t size = surface_.width_ * surface_.height_ * 4;
  int32_t format;

  IonMemInfo mem_info;
  memset(&mem_info, 0x0, sizeof(IonMemInfo));
  auto ret = AllocateIonMemory(mem_info, size);
  if(0 != ret) {
    OVDBG_ERROR("%s:AllocateIonMemory failed",__func__);
    return ret;
  }
  OVDBG_DEBUG("%s: Ion memory allocated fd(%d)", __func__, mem_info.fd);

#if USE_CAIRO
  cr_surface_ = cairo_image_surface_create_for_data(static_cast<unsigned char*>
                                                    (mem_info.vaddr),
                                                    CAIRO_FORMAT_ARGB32,
                                                    surface_.width_,
                                                    surface_.height_,
                                                    surface_.width_ * 4);
  assert (cr_surface_ != nullptr);

  cr_context_ = cairo_create (cr_surface_);
  assert (cr_context_ != nullptr);
#endif

#if USE_CAIRO
  format = C2D_COLOR_FORMAT_8888_ARGB;
#endif
  ret = MapOverlaySurface(surface_, mem_info, format);
  if (ret) {
    OVDBG_ERROR("%s: Map failed!",__func__);
    goto ERROR;
  }

  OVDBG_VERBOSE("%s: Exit", __func__);
  return ret;
ERROR:
  close(surface_.ion_fd_);
  surface_.ion_fd_ = -1;
  return ret;
}

}; // namespace overlay

}; // namespace qmmf

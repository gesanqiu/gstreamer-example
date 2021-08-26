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

#pragma once

#include <adreno/c2d2.h>
#include <cutils/properties.h>
#include <ion/ion.h>
#include <linux/dma-buf.h>
#include <linux/msm_ion.h>
#include <linux/msm_kgsl.h>
#include <utils/String8.h>

#ifdef OVERLAY_OPEN_CL_BLIT
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif // OVERLAY_OPEN_CL_BLIT

#include "common/utils/qmmf_condition.h"

#if USE_SKIA
#include <SkCanvas.h>
#elif USE_CAIRO
#include <cairo/cairo.h>
#endif

namespace qmmf {

namespace overlay {

/**
OVDBG_INFO, ERROR and WARN logs are enabled all the time by default.
*/
#define OVDBG_INFO(fmt, args...)  ALOGD(fmt, ##args)
#define OVDBG_ERROR(fmt, args...) ALOGE(fmt, ##args)
#define OVDBG_WARN(fmt, args...)  ALOGW(fmt, ##args)

// Remove comment markers to define LOG_LEVEL_DEBUG for debugging-related logs
//#define LOG_LEVEL_DEBUG

// Remove comment markers to define LOG_LEVEL_VERBOSE for complete logs
//#define LOG_LEVEL_VERBOSE

#ifdef LOG_LEVEL_DEBUG
#define OVDBG_DEBUG(fmt, args...)  ALOGD(fmt, ##args)
#else
#define OVDBG_DEBUG(...) ((void)0)
#endif

#ifdef LOG_LEVEL_VERBOSE
#define OVDBG_VERBOSE(fmt, args...)  ALOGD(fmt, ##args)
#else
#define OVDBG_VERBOSE(...) ((void)0)
#endif

#define OVERLAYITEM_X_MARGIN_PERCENT  0.5
#define OVERLAYITEM_Y_MARGIN_PERCENT  0.5
#define MAX_LEN       128
#define MAX_OVERLAYS  10
#define BG_TRANSPARENT_COLOR 0xFFFFFF00
#define BG_DEBUG_COLOR       0xFFE5CC80 //Light gray.
#define DOWNSCALE_FACTOR     4

#define BLIT_KERNEL      "/usr/lib/qmmf/overlay_blit_kernel.cl"
#define BLIT_KERNEL_NAME "overlay_cl"

// Remove comment marker to enable backgroud surface drawing of overlay objects.
//#define DEBUG_BACKGROUND_SURFACE

// Remove comment marker to measure time taken in overlay drawing.
//#define DEBUG_BLIT_TIME

#define PROP_DUMP_BLOB_IMAGE        "persist.qmmf.overlay.dump.blob"
#define PROP_BOX_STROKE_WIDTH       "persist.qmmf.overlay.stroke.width"


#ifdef OVERLAY_OPEN_CL_BLIT

struct OpenClFrame {
  cl_mem    cl_buffer;
  cl_uint   plane0_offset;
  cl_uint   plane1_offset;
  cl_ushort stride0;
  cl_ushort stride1;
  cl_ushort swap_uv;
};

struct OpenCLArgs {
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
    cl_mem   mask;
};

class OpenClKernel {
public:

  OpenClKernel(const std::string &kernel_name) :
                    kernel_name_(kernel_name),
                    prog_(nullptr),
                    kernel_(nullptr),
                    kernel_dimensions_(2),
                    local_size_{0, 0},
                    global_size_{0, 0},
                    global_offset_{0, 0} {}

  OpenClKernel(const OpenClKernel &other) :
                    kernel_name_(other.kernel_name_),
                    prog_(other.prog_),
                    kernel_(nullptr),
                    kernel_dimensions_(other.kernel_dimensions_),
                    local_size_{0, 0},
                    global_size_{0, 0},
                    global_offset_{0, 0} {}

  ~OpenClKernel();

  static std::shared_ptr<OpenClKernel> New(const std::string &path_to_src,
                                           const std::string &name);

  std::shared_ptr<OpenClKernel> AddInstance();

  int32_t BuildProgram(const std::string &path_to_src);

  int32_t SetKernelArgs(OpenClFrame &frame, OpenCLArgs &args);

  int32_t RunCLKernel(bool wait_to_finish);

  static int32_t MapBuffer(cl_mem &cl_buffer, void *vaddr, int32_t fd,
                            uint32_t size);

  static int32_t UnMapBuffer(cl_mem &cl_buffer);

  static int32_t MapImage(cl_mem &cl_buffer, void *vaddr, int32_t fd,
                          size_t width, size_t height, uint32_t stride);

  static int32_t unMapImage(cl_mem &cl_buffer);

private:

  static int32_t OpenCLInit();

  static int32_t OpenCLDeInit();

  int32_t CreateKernelInstance();

  static void ClCompleteCallback(cl_event event,
                                 cl_int event_command_exec_status,
                                 void *user_data);

  std::string CreateCLKernelBuildLog();

  static cl_device_id device_id_;
  static cl_context context_;
  static cl_command_queue command_queue_;
  static std::mutex lock_;
  static int32_t ref_count;

  std::string kernel_name_;
  cl_program prog_;
  cl_kernel kernel_;
  cl_uint kernel_dimensions_;
  size_t local_size_[2];
  size_t global_size_[2];
  size_t global_offset_[2];

  static const uint32_t kWaitProcessTimeout = 2000000000; // 2 sec.

  struct SyncObject {
    bool       done_;
    QCondition signal_;
    std::mutex lock_;
  } sync_;
};

#endif // OVERLAY_OPEN_CL_BLIT

struct DrawInfo {
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
#ifdef OVERLAY_OPEN_CL_BLIT
    cl_mem   mask;
    std::shared_ptr<OpenClKernel> blit_inst;
#else
    uint32_t c2dSurfaceId;
#endif
    uint32_t in_width;
    uint32_t in_height;
    uint32_t in_x;
    uint32_t in_y;
};

struct RGBAValues {
  double red;
  double green;
  double blue;
  double alpha;
};

struct C2dObjects {
  C2D_OBJECT objects[MAX_OVERLAYS*2];
};

class OverlaySurface {
 public:
  OverlaySurface () : width_(0), height_(0), gpu_addr_(nullptr),
     vaddr_(nullptr), ion_fd_(0), size_(0) {
#ifdef OVERLAY_OPEN_CL_BLIT
      cl_buffer_ = nullptr;
      blit_inst_ = nullptr;
#else // OVERLAY_OPEN_CL_BLIT
      c2dsurface_id_ = -1;
#endif // OVERLAY_OPEN_CL_BLIT
     }

  uint32_t                      width_;
  uint32_t                      height_;
  void *                        gpu_addr_;
  void *                        vaddr_;
  int32_t                       ion_fd_;
  uint32_t                      size_;

#ifdef OVERLAY_OPEN_CL_BLIT
  cl_mem                        cl_buffer_;
  std::shared_ptr<OpenClKernel> blit_inst_;
#else // OVERLAY_OPEN_CL_BLIT
  uint32_t                      c2dsurface_id_;
#endif // OVERLAY_OPEN_CL_BLIT
};

//Base class for all types of overlays.
class OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItem(int32_t ion_device, OverlayType type,
              std::shared_ptr<OpenClKernel> &blit);
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItem(int32_t ion_device, OverlayType type);
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItem();

  virtual int32_t Init(OverlayParam& param) = 0 ;

  virtual int32_t UpdateAndDraw() = 0;

  virtual void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                           std::vector<DrawInfo>& draw_infos) = 0 ;

  virtual void GetParameters(OverlayParam& param) = 0;

  virtual int32_t UpdateParameters(OverlayParam& param) = 0;

  OverlayType& GetItemType() {return type_; }

  void MarkDirty(bool dirty);

  void Activate(bool value);

  bool IsActive() { return is_active_; }

 protected:

  struct IonMemInfo {
      uint32_t               size;
      int32_t                fd;
      void *                 vaddr;
  };

  int32_t AllocateIonMemory(IonMemInfo& mem_info, uint32_t size);

  void FreeIonMemory(void *&vaddr, int32_t &ion_fd, uint32_t size);

  int32_t MapOverlaySurface(OverlaySurface &surface, IonMemInfo &mem_info,
                            int32_t format);

  void UnMapOverlaySurface(OverlaySurface &surface);

  void ExtractColorValues(uint32_t hex_color, RGBAValues* color);

  virtual int32_t CreateSurface() = 0;

  void ClearSurface();

  virtual void DestroySurface();

  int32_t                x_;
  int32_t                y_;
  uint32_t               width_;
  uint32_t               height_;
  OverlaySurface         surface_;
  OverlayLocationType    location_type_;
  bool                   dirty_;
  int32_t                ion_device_;
  OverlayType            type_;
#if USE_CAIRO
  cairo_surface_t*       cr_surface_;
  cairo_t*               cr_context_;
#endif
 private:
  bool                   is_active_;
};

class OverlayItemStaticImage : public OverlayItem {

 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemStaticImage(int32_t ion_device,
                         std::shared_ptr<OpenClKernel> &blit)
                    : OverlayItem(ion_device, OverlayType::kStaticImage, blit),
                      image_path_() {};
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemStaticImage(int32_t ion_device)
                    : OverlayItem(ion_device, OverlayType::kStaticImage),
                      image_path_() {};
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemStaticImage();

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
      std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;
 private:
  int32_t CreateSurface();

  void DestroySurface();

  android::String8 image_path_;
  OverlayImageType image_type_;
  char *   image_buffer_;
  uint32_t image_size_;
  uint32_t crop_rect_x_;
  uint32_t crop_rect_y_;
  uint32_t crop_rect_width_;
  uint32_t crop_rect_height_;
  bool     blob_image_dump_enabled_;
  int32_t  blob_buffer_file_fd_;
  bool blob_buffer_updated_;
  std::mutex update_param_lock_;
};

class OverlayItemDateAndTime: public OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemDateAndTime(int32_t ion_device,
                         std::shared_ptr<OpenClKernel> &blit);
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemDateAndTime(int32_t ion_device);
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemDateAndTime();

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                   std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;

 private:
  static const int kTextSize             = 20;
  static const int kCairoBufferMinWidth  = kTextSize * 6;
  static const int kCairoBufferMinHeight = kTextSize * 2;

  int32_t CreateSurface();

  OverlayDateTimeType date_time_type_;
  uint32_t            text_color_;
  time_t              prev_time_;
#if USE_SKIA
  SkCanvas*           canvas_;
#endif
};

class OverlayItemBoundingBox: public OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemBoundingBox(int32_t ion_device,
                         std::shared_ptr<OpenClKernel> &blit);
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemBoundingBox(int32_t ion_device);
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemBoundingBox();

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                   std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;

 private:

  static const int32_t kBoxBuffWidth = 320;
  static const int32_t kStrokeWidth  = 4;
  static const int32_t kTextLimit    = 20;
  static const int32_t kTextSize     = 25;
  static const int32_t kTextPercent  = 20;
  static const int32_t kTextMargin   = kStrokeWidth + 4;

  int32_t CreateSurface();

  void ClearTextSurface();

  void DestroyTextSurface();

  uint32_t    bbox_color_;
#if USE_SKIA
  SkCanvas*            canvas_;
#endif
  android::String8  bbox_name_;
  uint32_t          text_height_   = 0;

#if USE_CAIRO
  OverlaySurface    text_surface_;
  uint32_t          box_stroke_width_;
  cairo_surface_t*  text_cr_surface_;
  cairo_t*          text_cr_context_;
#endif
};

class OverlayItemText: public OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemText(int32_t ion_device, std::shared_ptr<OpenClKernel> &blit)
                      : OverlayItem(ion_device, OverlayType::kUserText, blit),
                        text_() {};
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemText(int32_t ion_device)
                      : OverlayItem(ion_device, OverlayType::kUserText),
                        text_() {};
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemText();

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                   std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;

 private:
  static const uint32_t kTextSize             = 40;
  static const uint32_t kCairoBufferMinWidth  = kTextSize * 4;
  static const uint32_t kCairoBufferMinHeight = kTextSize;

  int32_t CreateSurface();

  uint32_t          text_color_;
  std::string       text_;
#if USE_SKIA
  SkCanvas*         canvas_;
#endif

};

class OverlayItemPrivacyMask: public OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemPrivacyMask(int32_t ion_device,
                         std::shared_ptr<OpenClKernel> &blit)
                  : OverlayItem(ion_device, OverlayType::kPrivacyMask, blit) {};
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemPrivacyMask(int32_t ion_device)
                  : OverlayItem(ion_device, OverlayType::kPrivacyMask) {};
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemPrivacyMask() {};

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                   std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;

 private:

  static const uint32_t kMaskBoxBufWidth = 1920;

  int32_t CreateSurface();
#if USE_SKIA
  SkCanvas*   canvas_;
#endif
  uint32_t    mask_color_;
  OverlayPrivacyMask config_;
};

class OverlayItemGraph : public OverlayItem {
 public:

#ifdef OVERLAY_OPEN_CL_BLIT
  OverlayItemGraph(int32_t ion_device,
                   std::shared_ptr<OpenClKernel> &blit)
                      : OverlayItem(ion_device, OverlayType::kGraph, blit) {};
#else // OVERLAY_OPEN_CL_BLIT
  OverlayItemGraph(int32_t ion_device)
                      : OverlayItem(ion_device, OverlayType::kGraph) {};
#endif // OVERLAY_OPEN_CL_BLIT

  virtual ~OverlayItemGraph() {};

  int32_t Init(OverlayParam& param) override;

  int32_t UpdateAndDraw() override;

  void GetDrawInfo(uint32_t target_width, uint32_t target_height,
                   std::vector<DrawInfo>& draw_infos) override;

  void GetParameters(OverlayParam& param) override;

  int32_t UpdateParameters(OverlayParam& param) override;

 private:

  int32_t CreateSurface();

  static const int  kDotRadius = 3;
  static const int  kLineWidth = 2;
  static const int  kGraphBufWidth = 480;
  static const int  kGraphBufHeight = 270;

  uint32_t          graph_color_;
  float             downscale_ratio_;
  OverlayGraph      graph_;
};


}; // namespace overlay
}; // namespace qmmf

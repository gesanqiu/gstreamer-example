/*
* Copyright (c) 2020, The Linux Foundation. All rights reserved.
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

#ifndef __GST_QTI_OVERLAY_H__
#define __GST_QTI_OVERLAY_H__

#include <gmodule.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>
#include <qmmf-sdk/qmmf_overlay.h>

using namespace qmmf::overlay;

G_BEGIN_DECLS

#define GST_TYPE_OVERLAY \
  (gst_overlay_get_type())
#define GST_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OVERLAY,GstOverlay))
#define GST_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OVERLAY,GstOverlayClass))
#define GST_IS_OVERLAY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OVERLAY))
#define GST_IS_OVERLAY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OVERLAY))
#define GST_OVERLAY_CAST(obj)       ((GstOverlay *)(obj))

typedef struct _GstOverlay GstOverlay;
typedef struct _GstOverlayClass GstOverlayClass;
typedef struct _GstOverlayUser GstOverlayUser;
typedef struct _GstOverlayUsrText GstOverlayUsrText;
typedef struct _GstOverlayUsrDate GstOverlayUsrDate;
typedef struct _GstOverlayUsrSImg GstOverlayUsrSImg;
typedef struct _GstOverlayUsrBBox GstOverlayUsrBBox;
typedef struct _GstOverlayUsrMask GstOverlayUsrMask;
typedef struct _GstOverlayString GstOverlayString;

struct _GstOverlay {
  GstVideoFilter      parent;
  Overlay             *overlay;
  TargetBufferFormat  format;
  guint               width;
  guint               height;
  GMutex              lock;
  gboolean            meta_color;

  /* Machine learning overlay */
  GSequence           *bbox_id;
  GSequence           *simg_id;
  GSequence           *text_id;
  GSequence           *pose_id;

  guint               bbox_color;
  guint               date_color;
  guint               text_color;
  guint               pose_color;

  GstVideoRectangle   text_dest_rect;

  /* User overlay */
  GSequence           *usr_text;
  GSequence           *usr_date;
  GSequence           *usr_simg;
  GSequence           *usr_bbox;
  GSequence           *usr_mask;
};

struct _GstOverlayClass {
  GstVideoFilterClass parent;
};

/* GstOverlayUser - common parameters for all user overlays
 * user_id: overlay user instance id
 * item_id: overlay HW instacne id
 * is_applied: flag indicating if new configuration is applied
 * user_data: user pointer which is used in release handler
 */
struct _GstOverlayUser {
  gchar                 *user_id;
  guint                 item_id;
  gboolean              is_applied;
  gpointer              user_data;
};


/* GstOverlayUsrText - parameters for user text overlay
 * base: common parameters for all user overlays
 * text: user text
 * color: overlay color
 * dest_rect: render destination rectangle in video stream
 */
struct _GstOverlayUsrText {
  GstOverlayUser        base;
  gchar                 *text;
  guint                 color;
  GstVideoRectangle     dest_rect;
};

/* GstOverlayUsrDate - parameters for user date overlay
 * base: common parameters for all user overlays
 * date_format: date format
 * time_format: time format
 * color: overlay color
 * dest_rect: render destination rectangle in video stream
 */
struct _GstOverlayUsrDate {
  GstOverlayUser        base;
  OverlayDateFormatType date_format;
  OverlayTimeFormatType time_format;
  guint                 color;
  GstVideoRectangle     dest_rect;
};

/* GstOverlayUsrSImg - parameters for user static image overlay
 * base: common parameters for all user overlays
 * img_file: image file name with full path
 * img_width: image width
 * img_height: image height
 * img_buffer: pointer to image buffer
 * img_size: image buffer size
 * dest_rect: render destination rectangle in video stream
 */
struct _GstOverlayUsrSImg {
  GstOverlayUser        base;
  gchar                 *img_file;
  guint                 img_width;
  guint                 img_height;
  gchar                 *img_buffer;
  gsize                 img_size;
  GstVideoRectangle     dest_rect;
};

/* GstOverlayUsrBBox - parameters for user bounding box overlay
 * base: common parameters for all user overlays
 * label: bounding box label
 * boundind_box: boundind box rectangle
 * color: overlay color
 */
struct _GstOverlayUsrBBox {
  GstOverlayUser        base;
  gchar                 *label;
  GstVideoRectangle     boundind_box;
  guint                 color;
};

/* GstOverlayUsrMask - parameters for privacy mask overlay
 * base: common parameters for all user overlays
 * type: privacy mask type
 * circle: circle dimensions
 * rectangle: rectangle dimensions
 * color: overlay color
 * dest_rect: render destination rectangle in video stream
 */
struct _GstOverlayUsrMask {
  GstOverlayUser        base;
  OverlayPrivacyMaskType type;
  Overlaycircle         circle;
  OverlayRect           rectangle;
  guint                 color;
  GstVideoRectangle     dest_rect;
};

/* GstOverlayString - pair for string and capacity
 * string: pointer to string
 * capacity: size of the storage space currently allocated for the string
 */
struct _GstOverlayString {
  gchar *string;
  guint  capacity;
};

G_GNUC_INTERNAL GType gst_overlay_get_type (void);

#define OVERLAY_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state) \
    ((pspec->flags & GST_PARAM_MUTABLE_PLAYING) ? (state <= GST_STATE_PLAYING) \
        : ((pspec->flags & GST_PARAM_MUTABLE_PAUSED) ? (state <= GST_STATE_PAUSED) \
            : ((pspec->flags & GST_PARAM_MUTABLE_READY) ? (state <= GST_STATE_READY) \
                : (state <= GST_STATE_NULL))))

G_END_DECLS

#endif // __GST_QTI_OVERLAY_H__

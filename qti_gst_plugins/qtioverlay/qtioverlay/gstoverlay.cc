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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/allocators/gstfdmemory.h>
#include <ml-meta/ml_meta.h>

#include "gstoverlay.h"

#define GST_CAT_DEFAULT overlay_debug
GST_DEBUG_CATEGORY_STATIC (overlay_debug);

#define gst_overlay_parent_class parent_class
G_DEFINE_TYPE (GstOverlay, gst_overlay, GST_TYPE_VIDEO_FILTER);

#undef GST_VIDEO_SIZE_RANGE
#define GST_VIDEO_SIZE_RANGE "(int) [ 1, 32767]"

#define GST_VIDEO_FORMATS "{ NV12, NV21 }"

#define DEFAULT_PROP_OVERLAY_TEXT        NULL
#define DEFAULT_PROP_OVERLAY_DATE        NULL
#define DEFAULT_PROP_OVERLAY_SIMG        NULL
#define DEFAULT_PROP_OVERLAY_BBOX        NULL
#define DEFAULT_PROP_OVERLAY_META_COLOR  false
#define DEFAULT_PROP_OVERLAY_BBOX_COLOR  kColorBlue
#define DEFAULT_PROP_OVERLAY_DATE_COLOR  kColorRed
#define DEFAULT_PROP_OVERLAY_TEXT_COLOR  kColorYellow
#define DEFAULT_PROP_OVERLAY_POSE_COLOR  kColorLightGreen
#define DEFAULT_PROP_OVERLAY_MASK_COLOR  kColorDarkGray

#define DEFAULT_PROP_DEST_RECT_X      40
#define DEFAULT_PROP_DEST_RECT_Y      40
#define DEFAULT_PROP_DEST_RECT_WIDTH  200
#define DEFAULT_PROP_DEST_RECT_HEIGHT 40


/* This is initial value. Size is recalculated runtime and buffer is
 * reallocated runtime.
 */
#define GST_OVERLAY_TO_STRING_SIZE 256
#define GST_OVERLAY_TEXT_STRING_SIZE 80
#define GST_OVERLAY_DATE_STRING_SIZE 100
#define GST_OVERLAY_SIMG_STRING_SIZE 100
#define GST_OVERLAY_BBOX_STRING_SIZE 80
#define GST_OVERLAY_MASK_STRING_SIZE 100

#define GST_OVERLAY_UNUSED(var) ((void)var)

static GstMLKeyPointsType PoseChain [][2] {
  {LEFT_SHOULDER,  RIGHT_SHOULDER},
  {LEFT_SHOULDER,  LEFT_ELBOW},
  {LEFT_SHOULDER,  LEFT_HIP},
  {RIGHT_SHOULDER, RIGHT_ELBOW},
  {RIGHT_SHOULDER, RIGHT_HIP},
  {LEFT_ELBOW,     LEFT_WRIST},
  {RIGHT_ELBOW,    RIGHT_WRIST},
  {LEFT_HIP,       RIGHT_HIP},
  {LEFT_HIP,       LEFT_KNEE},
  {RIGHT_HIP,      RIGHT_KNEE},
  {LEFT_KNEE,      LEFT_ANKLE},
  {RIGHT_KNEE,     RIGHT_ANKLE}
};

/* Supported GST properties
 * PROP_OVERLAY_TEXT - overlays user defined text
 * PROP_OVERLAY_DATE - overlays date and time
 * PROP_OVERLAY_SIMG - overlays static image
 * PROP_OVERLAY_BBOX - overlays bounding box
 * PROP_OVERLAY_MASK - overlays privacy mask
 * PROP_OVERLAY_META_COLOR - Use color from meta data
 * PROP_OVERLAY_BBOX_COLOR - ML Detection color
 * PROP_OVERLAY_DATE_COLOR - ML Time and Date color
 * PROP_OVERLAY_TEXT_COLOR - ML Classification color
 * PROP_OVERLAY_POSE_COLOR - ML PoseNet color
 * PROP_OVERLAY_TEXT_DEST_RECT - ML Classification destination rectangle
 */
enum {
  PROP_0,
  PROP_OVERLAY_TEXT,
  PROP_OVERLAY_DATE,
  PROP_OVERLAY_SIMG,
  PROP_OVERLAY_BBOX,
  PROP_OVERLAY_MASK,
  PROP_OVERLAY_META_COLOR,
  PROP_OVERLAY_BBOX_COLOR,
  PROP_OVERLAY_DATE_COLOR,
  PROP_OVERLAY_TEXT_COLOR,
  PROP_OVERLAY_POSE_COLOR,
  PROP_OVERLAY_TEXT_DEST_RECT
};

static GstStaticCaps gst_overlay_format_caps =
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS) ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS));

/**
 * GstOverlayMetaApplyFunc:
 * @gst_overlay: context
 * @meta: metadata payload
 * @item_id: overlay item instance id
 *
 * API for overlay configuration by metadata.
 */
typedef gboolean (* GstOverlayMetaApplyFunc)
    (GstOverlay *gst_overlay, gpointer meta, uint32_t * item_id);

/**
 * GstOverlaySetFunc:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * API for overlay configuration by GST property.
 */
typedef gboolean (* GstOverlaySetFunc)
    (GstOverlayUser * entry, GstStructure * structure, gboolean entry_exist);

/**
 * GstOverlayGetFunc:
 * @data: input structure
 * @user_data: output string
 *
 * API for quering overlay configuration by GST property.
 */
typedef void (* GstOverlayGetFunc) (gpointer data, gpointer user_data);

/**
 * gst_overlay_caps:
 *
 * Expose overlay pads capabilities.
 */
static GstCaps *
gst_overlay_caps (void)
{
  static GstCaps *caps = NULL;
  static volatile gsize inited = 0;
  if (g_once_init_enter (&inited)) {
    caps = gst_static_caps_get (&gst_overlay_format_caps);
    g_once_init_leave (&inited, 1);
  }
  return caps;
}

/**
 * gst_overlay_src_template:
 *
 * Expose overlay source pads capabilities.
 */
static GstPadTemplate *
gst_overlay_src_template (void)
{
  return gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      gst_overlay_caps ());
}

/**
 * gst_overlay_sink_template:
 *
 * Expose overlay sink pads capabilities.
 */
static GstPadTemplate *
gst_overlay_sink_template (void)
{
  return gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      gst_overlay_caps ());
}

/**
 * gst_overlay_destroy_overlay_item:
 * @data: pointer to overlay item instance id
 * @user_data: context
 *
 * Destroy overlay instance and reset overlay instance id.
 */
static void
gst_overlay_destroy_overlay_item (gpointer data, gpointer user_data)
{
  uint32_t *item_id = (uint32_t *) data;
  Overlay *overlay = (Overlay *) user_data;

  int32_t ret = overlay->DisableOverlayItem (*item_id);
  if (ret != 0) {
    GST_ERROR ("Overlay %d disable failed!", *item_id);
  }

  ret = overlay->DeleteOverlayItem (*item_id);
  if (ret != 0) {
    GST_ERROR ("Overlay %d delete failed!", *item_id);
  }

  *item_id = 0;
}

/**
 * gst_overlay_apply_item_list:
 * @gst_overlay: context
 * @meta_list: List of metadata entries
 * @apply_func: overlay configuration API. Converts metadata to overlay
 *              configuration and applies it
 * @ov_id: overlay item instance id handlers
 *
 * Iterates list of metadata entries and call provided overlay configuration
 * API for each of them. Overlay instances ids are also managed by this
 * function.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_item_list (GstOverlay *gst_overlay,
    GSList * meta_list, GstOverlayMetaApplyFunc apply_func, GSequence * ov_id)
{
  gboolean res = TRUE;

  guint meta_num = g_slist_length (meta_list);
  if (meta_num) {
    for (uint32_t i = g_sequence_get_length (ov_id);
        i < meta_num; i++) {
      g_sequence_append(ov_id, calloc(1, sizeof(uint32_t)));
    }

    for (uint32_t i = 0; i < meta_num; i++) {
      res = apply_func (gst_overlay, g_slist_nth_data (meta_list, 0),
          (uint32_t *) g_sequence_get (g_sequence_get_iter_at_pos (ov_id, i)));
      if (!res) {
        GST_ERROR_OBJECT (gst_overlay, "Overlay create failed!");
        return res;
      }
      meta_list = meta_list->next;
    }
  }
  if ((guint) g_sequence_get_length (ov_id) > meta_num) {
    g_sequence_foreach_range (
        g_sequence_get_iter_at_pos (ov_id, meta_num),
        g_sequence_get_end_iter (ov_id),
        gst_overlay_destroy_overlay_item, gst_overlay->overlay);
    g_sequence_remove_range (
        g_sequence_get_iter_at_pos (ov_id, meta_num),
        g_sequence_get_end_iter (ov_id));
  }

  return TRUE;
}

/**
 * gst_overlay_apply_bbox_item:
 * @gst_overlay: context
 * @bbox: bounding box rectangle
 * @label: bounding box label
 * @color: text overlay
 * @item_id: pointer to overlay item instance id
 *
 * Configures and enables bounding box overlay instance.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_bbox_item (GstOverlay * gst_overlay, GstVideoRectangle * bbox,
    gchar * label, guint color, uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (bbox != NULL, FALSE);
  g_return_val_if_fail (label != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kBoundingBox;
    ov_param.location = OverlayLocationType::kTopLeft;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.color = color;
  ov_param.dst_rect.start_x = bbox->x;
  ov_param.dst_rect.start_y = bbox->y;
  ov_param.dst_rect.width = bbox->w;
  ov_param.dst_rect.height = bbox->h;

  if (sizeof (ov_param.bounding_box.box_name) <= strlen (label)) {
    GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d <= %d",
        sizeof (ov_param.bounding_box.box_name), strlen (label));
    return FALSE;
  }
  g_strlcpy (ov_param.bounding_box.box_name, label,
      sizeof (ov_param.bounding_box.box_name));

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_ml_bbox_item:
 * @gst_overlay: context
 * @metadata: machine learning metadata entry
 * @item_id: pointer to overlay item instance id
 *
 * Converts GstMLDetectionMeta metadata to overlay configuration and applies it
 * as bounding box overlay.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_ml_bbox_item (GstOverlay * gst_overlay, gpointer metadata,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLDetectionMeta * meta = (GstMLDetectionMeta *) metadata;
  GstMLClassificationResult * result =
      (GstMLClassificationResult *) g_slist_nth_data (meta->box_info, 0);

  GstVideoRectangle bbox;
  bbox.x = meta->bounding_box.x;
  bbox.y = meta->bounding_box.y;
  bbox.w = meta->bounding_box.width;
  bbox.h = meta->bounding_box.height;
  if (gst_overlay->meta_color) gst_overlay->bbox_color = meta->bbox_color;

  return gst_overlay_apply_bbox_item (gst_overlay, &bbox, result->name,
      gst_overlay->bbox_color, item_id);
}

/**
 * gst_overlay_apply_user_bbox_item:
 * @data: context
 * @user_data: overlay configuration of GstOverlayUsrBBox type
 *
 * Configures text overlay instance with user provided configuration
 * and enables it.
 */
static void
gst_overlay_apply_user_bbox_item (gpointer data, gpointer user_data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (user_data != NULL);

  GstOverlay * gst_overlay = (GstOverlay *) user_data;
  GstOverlayUsrBBox * ov_data = (GstOverlayUsrBBox *) data;

  if (!ov_data->base.is_applied) {
    gboolean res = gst_overlay_apply_bbox_item (gst_overlay,
        &ov_data->boundind_box, ov_data->label, ov_data->color,
        &ov_data->base.item_id);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "User overlay apply failed!");
      return;
    }
    ov_data->base.is_applied = TRUE;
  }
}

/**
 * gst_overlay_apply_simg_item:
 * @gst_overlay: context
 * @img_buffer: pointer to image buffer
 * @img_size: image buffer size
 * @src_rect: represent image dimension in buffer
 * @dst_rect: render destination rectangle in video stream
 * @item_id: pointer to overlay item instance id
 *
 * Configures and enables static image overlay instance.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_simg_item (GstOverlay *gst_overlay, gpointer img_buffer,
    guint img_size, GstVideoRectangle *src_rect, GstVideoRectangle *dst_rect,
    uint32_t *item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (img_buffer != NULL, FALSE);
  g_return_val_if_fail (src_rect != NULL, FALSE);
  g_return_val_if_fail (dst_rect != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);


  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kStaticImage;
    ov_param.image_info.image_type = OverlayImageType::kBlobType;
    ov_param.location = OverlayLocationType::kRandom;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.dst_rect.start_x = dst_rect->x;
  ov_param.dst_rect.start_y = dst_rect->y;
  ov_param.dst_rect.width = dst_rect->w;
  ov_param.dst_rect.height = dst_rect->h;

  ov_param.image_info.source_rect.start_x = src_rect->x;
  ov_param.image_info.source_rect.start_y = src_rect->y;
  ov_param.image_info.source_rect.width = src_rect->w;
  ov_param.image_info.source_rect.height = src_rect->h;
  ov_param.image_info.image_buffer = (char *)img_buffer;
  ov_param.image_info.image_size = img_size;
  ov_param.image_info.buffer_updated = true;

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_ml_simg_item:
 * @gst_overlay: context
 * @metadata: machine learning metadata entry
 * @item_id: pointer to overlay item instance id
 *
 * Converts GstMLSegmentationMeta metadata to overlay configuration and applies
 * it as static image overlay.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_ml_simg_item (GstOverlay *gst_overlay, gpointer metadata,
    uint32_t *item_id)
{
  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLSegmentationMeta *meta = (GstMLSegmentationMeta *) metadata;

  GstVideoRectangle dst_rect;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.w = gst_overlay->width;
  dst_rect.h = gst_overlay->height;

  GstVideoRectangle src_rect;
  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.w = meta->img_width;
  src_rect.h = meta->img_height;

  return gst_overlay_apply_simg_item (gst_overlay, meta->img_buffer,
    meta->img_size, &src_rect, &dst_rect, item_id);
}

/**
 * gst_overlay_apply_user_simg_item:
 * @data: context
 * @user_data: overlay configuration of GstOverlayUsrSImg type
 *
 * Configures static image overlay instance with user provided configuration
 * and enables it.
 */
static void
gst_overlay_apply_user_simg_item (gpointer data, gpointer user_data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (user_data != NULL);

  GstOverlay * gst_overlay = (GstOverlay *) user_data;
  GstOverlayUsrSImg * ov_data = (GstOverlayUsrSImg *) data;

  if (!ov_data->base.is_applied) {
    GstVideoRectangle dst_rect;
    dst_rect.x = ov_data->dest_rect.x;
    dst_rect.y = ov_data->dest_rect.y;
    dst_rect.w = ov_data->dest_rect.w;
    dst_rect.h = ov_data->dest_rect.h;

    GstVideoRectangle src_rect;
    src_rect.x = 0;
    src_rect.y = 0;
    src_rect.w = ov_data->img_width;
    src_rect.h = ov_data->img_height;

    gboolean res = gst_overlay_apply_simg_item (gst_overlay,
      ov_data->img_buffer, ov_data->img_size, &src_rect, &dst_rect,
      &ov_data->base.item_id);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "User overlay apply failed!");
      return;
    }
    ov_data->base.is_applied = TRUE;
  }
}

/**
 * gst_overlay_apply_text_item:
 * @gst_overlay: context
 * @name: overlay text
 * @color: text overlay
 * @location: render location in video stream
 * @dest_rect: render destination rectangle in video stream
 * @item_id: pointer to overlay item instance id
 *
 * Configures and enables text overlay instance.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_text_item (GstOverlay * gst_overlay, gchar * name,
    guint color, OverlayLocationType location, GstVideoRectangle * dest_rect,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (dest_rect != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kUserText;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.color = color;
  ov_param.location = OverlayLocationType::kNone;
  ov_param.dst_rect.start_x = dest_rect->x;
  ov_param.dst_rect.start_y = dest_rect->y;
  ov_param.dst_rect.width = dest_rect->w;
  ov_param.dst_rect.height = dest_rect->h;

  if (sizeof (ov_param.user_text) <= strlen (name)) {
    GST_ERROR_OBJECT (gst_overlay, "Text size exceeded %d <= %d",
      sizeof (ov_param.user_text), strlen (name));
    return FALSE;
  }
  g_strlcpy (ov_param.user_text, name, sizeof (ov_param.user_text));

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_ml_text_item:
 * @gst_overlay: context
 * @metadata: machine learning metadata entry
 * @item_id: pointer to overlay item instance id
 *
 * Converts GstMLClassificationMeta metadata to overlay configuration and
 * applies it as text overlay.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_ml_text_item (GstOverlay *gst_overlay, gpointer metadata,
    uint32_t * item_id)
{
  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLClassificationMeta * meta = (GstMLClassificationMeta *) metadata;

  return gst_overlay_apply_text_item (gst_overlay, meta->result.name,
    gst_overlay->text_color, OverlayLocationType::kTopLeft,
    &gst_overlay->text_dest_rect, item_id);
}

/**
 * gst_overlay_apply_user_text_item:
 * @data: context
 * @user_data: overlay configuration of GstOverlayUsrText type
 *
 * Configures text overlay instance with user provided configuration
 * and enables it.
 */
static void
gst_overlay_apply_user_text_item (gpointer data, gpointer user_data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (user_data != NULL);

  GstOverlay * gst_overlay = (GstOverlay *) user_data;
  GstOverlayUsrText * ov_data = (GstOverlayUsrText *) data;

  if (!ov_data->base.is_applied) {
    gboolean res = gst_overlay_apply_text_item (gst_overlay, ov_data->text,
      ov_data->color, OverlayLocationType::kNone, &ov_data->dest_rect,
      &ov_data->base.item_id);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "User overlay apply failed!");
      return;
    }
    ov_data->base.is_applied = TRUE;
  }
}

/**
 * gst_overlay_apply_ml_pose_item:
 * @gst_overlay: context
 * @metadata: machine learning metadata entry
 * @item_id: pointer to overlay item instance id
 *
 * Converts GstMLPoseNetMeta metadata to overlay configuration and applies
 * it as graph overlay.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_ml_pose_item (GstOverlay *gst_overlay, gpointer metadata,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (metadata != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  GstMLPoseNetMeta * pose = (GstMLPoseNetMeta *) metadata;

  static float kScoreTreshold = 0.1;

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kGraph;
    ov_param.color = gst_overlay->pose_color;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.dst_rect.start_x = 0;
  ov_param.dst_rect.start_y = 0;
  ov_param.dst_rect.width = gst_overlay->width;
  ov_param.dst_rect.height = gst_overlay->height;

  gint count = 0;
  gint points[KEY_POINTS_COUNT];

  if (pose->points[NOSE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[NOSE].x;
    ov_param.graph.points[count].y = pose->points[NOSE].y;
    points[NOSE] = count;
    count++;
  }

  if (pose->points[LEFT_EYE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_EYE].x;
    ov_param.graph.points[count].y = pose->points[LEFT_EYE].y;
    points[LEFT_EYE] = count;
    count++;
  }

  if (pose->points[RIGHT_EYE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_EYE].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_EYE].y;
    points[RIGHT_EYE] = count;
    count++;
  }

  if (pose->points[LEFT_EAR].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_EAR].x;
    ov_param.graph.points[count].y = pose->points[LEFT_EAR].y;
    points[LEFT_EAR] = count;
    count++;
  }

  if (pose->points[RIGHT_EAR].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_EAR].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_EAR].y;
    points[RIGHT_EAR] = count;
    count++;
  }

  if (pose->points[LEFT_SHOULDER].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_SHOULDER].x;
    ov_param.graph.points[count].y = pose->points[LEFT_SHOULDER].y;
    points[LEFT_SHOULDER] = count;
    count++;
  }

  if (pose->points[RIGHT_SHOULDER].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_SHOULDER].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_SHOULDER].y;
    points[RIGHT_SHOULDER] = count;
    count++;
  }

  if (pose->points[LEFT_ELBOW].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_ELBOW].x;
    ov_param.graph.points[count].y = pose->points[LEFT_ELBOW].y;
    points[LEFT_ELBOW] = count;
    count++;
  }

  if (pose->points[RIGHT_ELBOW].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_ELBOW].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_ELBOW].y;
    points[RIGHT_ELBOW] = count;
    count++;
  }

  if (pose->points[LEFT_WRIST].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_WRIST].x;
    ov_param.graph.points[count].y = pose->points[LEFT_WRIST].y;
    points[LEFT_WRIST] = count;
    count++;
  }

  if (pose->points[RIGHT_WRIST].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_WRIST].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_WRIST].y;
    points[RIGHT_WRIST] = count;
    count++;
  }

  if (pose->points[LEFT_HIP].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_HIP].x;
    ov_param.graph.points[count].y = pose->points[LEFT_HIP].y;
    points[LEFT_HIP] = count;
    count++;
  }

  if (pose->points[RIGHT_HIP].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_HIP].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_HIP].y;
    points[RIGHT_HIP] = count;
    count++;
  }

  if (pose->points[LEFT_KNEE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_KNEE].x;
    ov_param.graph.points[count].y = pose->points[LEFT_KNEE].y;
    points[LEFT_KNEE] = count;
    count++;
  }

  if (pose->points[RIGHT_KNEE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_KNEE].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_KNEE].y;
    points[RIGHT_KNEE] = count;
    count++;
  }

  if (pose->points[LEFT_ANKLE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[LEFT_ANKLE].x;
    ov_param.graph.points[count].y = pose->points[LEFT_ANKLE].y;
    points[LEFT_ANKLE] = count;
    count++;
  }

  if (pose->points[RIGHT_ANKLE].score > kScoreTreshold) {
    ov_param.graph.points[count].x = pose->points[RIGHT_ANKLE].x;
    ov_param.graph.points[count].y = pose->points[RIGHT_ANKLE].y;
    points[RIGHT_ANKLE] = count;
    count++;
  }
  ov_param.graph.points_count = count;

  count = 0;
  ov_param.graph.chain_count = 0;
  for (gint i = 0; i < sizeof (PoseChain) / sizeof (PoseChain[0]); i++) {
    GstMLKeyPointsType point0 = PoseChain[i][0];
    GstMLKeyPointsType point1 = PoseChain[i][1];
    if (pose->points[point0].score > kScoreTreshold &&
        pose->points[point1].score > kScoreTreshold) {
      ov_param.graph.chain[count][0] = points[point0];
      ov_param.graph.chain[count][1] = points[point1];
      count++;
    }
  }
  ov_param.graph.chain_count = count;

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_date_item:
 * @gst_overlay: context
 * @time_format: time format
 * @date_format: date format
 * @color:  date and time color
 * @location: render location in video stream
 * @dest_rect: render destination rectangle in video stream
 * @item_id: pointer to overlay item instance id
 *
 * Configures and enables date overlay instance.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_date_item (GstOverlay *gst_overlay,
    OverlayTimeFormatType time_format, OverlayDateFormatType date_format,
    guint color, OverlayLocationType location, GstVideoRectangle * dest_rect,
    uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kDateType;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.color = color;
  ov_param.location = location;
  ov_param.dst_rect.start_x = dest_rect->x;
  ov_param.dst_rect.start_y = dest_rect->y;
  ov_param.dst_rect.width = dest_rect->w;
  ov_param.dst_rect.height = dest_rect->h;
  ov_param.date_time.time_format = time_format;
  ov_param.date_time.date_format = date_format;

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_user_date_item:
 * @data: context
 * @user_data: overlay configuration of GstOverlayUsrDate type
 *
 * Configures date overlay instance with user provided configuration
 * and enables it.
 */
static void
gst_overlay_apply_user_date_item (gpointer data, gpointer user_data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (user_data != NULL);

  GstOverlay * gst_overlay = (GstOverlay *) user_data;
  GstOverlayUsrDate * ov_data = (GstOverlayUsrDate *) data;

  if (!ov_data->base.is_applied) {
    gboolean res = gst_overlay_apply_date_item (gst_overlay,
      ov_data->time_format, ov_data->date_format, ov_data->color,
      OverlayLocationType::kNone, &ov_data->dest_rect, &ov_data->base.item_id);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "User overlay apply failed!");
      return;
    }
    ov_data->base.is_applied = TRUE;
  }
}

/**
 * gst_overlay_apply_mask_item:
 * @gst_overlay: context
 * @circle: circle dimensions
 * @rectangle: rectangle dimensions
 * @color: privacy mask color
 * @dest_rect: render destination rectangle in video stream
 * @item_id: pointer to overlay item instance id
 *
 * Configures and enables privacy mask overlay instance.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_mask_item (GstOverlay * gst_overlay,
    OverlayPrivacyMaskType type, Overlaycircle *circle, OverlayRect *rectangle,
    guint color, GstVideoRectangle * dest_rect, uint32_t * item_id)
{
  OverlayParam ov_param;
  int32_t ret = 0;

  g_return_val_if_fail (gst_overlay != NULL, FALSE);
  g_return_val_if_fail (circle != NULL, FALSE);
  g_return_val_if_fail (rectangle != NULL, FALSE);
  g_return_val_if_fail (dest_rect != NULL, FALSE);
  g_return_val_if_fail (item_id != NULL, FALSE);

  if (!(*item_id)) {
    ov_param = {};
    ov_param.type = OverlayType::kPrivacyMask;
  } else {
    ret = gst_overlay->overlay->GetOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay get param failed! ret: %d", ret);
      return FALSE;
    }
  }

  ov_param.color = color;
  ov_param.location = OverlayLocationType::kNone;
  ov_param.dst_rect.start_x = dest_rect->x;
  ov_param.dst_rect.start_y = dest_rect->y;
  ov_param.dst_rect.width = dest_rect->w;
  ov_param.dst_rect.height = dest_rect->h;

  ov_param.privacy_mask.type = type;
  if (type == OverlayPrivacyMaskType::kInverseRectangle ||
      type == OverlayPrivacyMaskType::kRectangle) {
    ov_param.privacy_mask.rectangle = *rectangle;
  } else {
    ov_param.privacy_mask.circle = *circle;
  }

  if (!(*item_id)) {
    ret = gst_overlay->overlay->CreateOverlayItem (ov_param, item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay create failed! ret: %d", ret);
      return FALSE;
    }

    ret = gst_overlay->overlay->EnableOverlayItem (*item_id);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay enable failed! ret: %d", ret);
      return FALSE;
    }
  } else {
    ret = gst_overlay->overlay->UpdateOverlayParams (*item_id, ov_param);
    if (ret != 0) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay set param failed! ret: %d", ret);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * gst_overlay_apply_user_mask_item:
 * @data: context
 * @user_data: overlay configuration of GstOverlayUsrMask type
 *
 * Configures privacy mask overlay instace with user provided configuration
 * and enables it.
 */
static void
gst_overlay_apply_user_mask_item (gpointer data, gpointer user_data)
{
  g_return_if_fail (data != NULL);
  g_return_if_fail (user_data != NULL);

  GstOverlay * gst_overlay = (GstOverlay *) user_data;
  GstOverlayUsrMask * ov_data = (GstOverlayUsrMask *) data;

  if (!ov_data->base.is_applied) {
    gboolean res = gst_overlay_apply_mask_item (gst_overlay, ov_data->type,
        &ov_data->circle, &ov_data->rectangle, ov_data->color,
        &ov_data->dest_rect, &ov_data->base.item_id);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "User overlay apply failed!");
      return;
    }
    ov_data->base.is_applied = TRUE;
  }
}

/**
 * gst_overlay_apply_overlay:
 * @gst_overlay: context
 * @frame: GST video frame
 *
 * Renders all created overlay instances on top of a video frame.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_apply_overlay (GstOverlay *gst_overlay, GstVideoFrame *frame)
{
  int32_t ret;
  GstMemory *memory = gst_buffer_peek_memory (frame->buffer, 0);
  guint fd = gst_fd_memory_get_fd (memory);

  OverlayTargetBuffer overlay_buf;
  overlay_buf.width     = GST_VIDEO_FRAME_WIDTH (frame);
  overlay_buf.height    = GST_VIDEO_FRAME_HEIGHT (frame);
  overlay_buf.ion_fd    = fd;
  overlay_buf.frame_len = gst_buffer_get_size (frame->buffer);
  overlay_buf.format    = gst_overlay->format;

  ret = gst_overlay->overlay->ApplyOverlay (overlay_buf);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply failed!");
    return FALSE;
  }
  return TRUE;
}

/**
 * gst_overlay_set_text_overlay:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * This function parses user overlay configuration. Function is called when
 * overlay is configured by GST property.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_text_overlay (GstOverlayUser * entry, GstStructure * structure,
  gboolean entry_exist)
{
  GstOverlayUsrText * text_entry = (GstOverlayUsrText *) entry;
  gboolean color_set = FALSE;
  gboolean entry_valid = FALSE;

  for (gint idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);

    const GValue *value = NULL;
    value = gst_structure_get_value (structure, name);

    if (!g_strcmp0 (name, "text") && G_VALUE_HOLDS (value, G_TYPE_STRING)) {
      text_entry->text = g_strdup (g_value_get_string (value));
      if (strlen (text_entry->text) > 0) {
        entry_valid = TRUE;
      } else {
        GST_INFO ("String is empty. Stop overlay if exist");
        free (text_entry->text);
        return FALSE;
      }
    }

    if (!g_strcmp0 (name, "color")) {
      if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
        text_entry->color = g_value_get_uint (value);
        color_set = TRUE;
      }
      if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
        text_entry->color = (guint)g_value_get_int (value);
        color_set = TRUE;
      }
    }

    if (!g_strcmp0 (name, "dest-rect") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 4) {
      text_entry->dest_rect.x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      text_entry->dest_rect.y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      text_entry->dest_rect.w =
        g_value_get_int (gst_value_array_get_value (value, 2));
      text_entry->dest_rect.h =
        g_value_get_int (gst_value_array_get_value (value, 3));
    }
  }

  if (!color_set && entry_valid && !entry_exist) {
    text_entry->color = DEFAULT_PROP_OVERLAY_TEXT_COLOR;
  }

  return entry_valid;
}

/**
 * gst_overlay_set_date_overlay:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * This function parses user overlay configuration. Function is called when
 * overlay is configured by GST property.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_date_overlay (GstOverlayUser * entry, GstStructure * structure,
  gboolean entry_exist)
{
  GstOverlayUsrDate * date_entry = (GstOverlayUsrDate *) entry;
  gboolean color_set = FALSE;
  gboolean entry_valid = FALSE;
  gboolean date_valid = FALSE;
  gboolean time_valid = FALSE;

  for (gint idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);

    const GValue *value = NULL;
    value = gst_structure_get_value (structure, name);

    if (!g_strcmp0(name, "date-format") && G_VALUE_HOLDS (value, G_TYPE_STRING)) {
      if (!g_strcmp0 (g_value_get_string (value), "YYYYMMDD")) {
        date_entry->date_format = OverlayDateFormatType::kYYYYMMDD;
      } else if (!g_strcmp0 (g_value_get_string (value), "MMDDYYYY")) {
        date_entry->date_format = OverlayDateFormatType::kMMDDYYYY;
      } else {
        GST_ERROR ("Unsupported date format %s", g_value_get_string (value));
        return FALSE;
      }
      date_valid = TRUE;
    }

    if (!g_strcmp0(name, "time-format") && G_VALUE_HOLDS (value, G_TYPE_STRING)) {
      if (!g_strcmp0 (g_value_get_string (value), "HHMMSS_24HR")) {
        date_entry->time_format = OverlayTimeFormatType::kHHMMSS_24HR;
      } else if (!g_strcmp0 (g_value_get_string (value), "HHMMSS_AMPM")) {
        date_entry->time_format = OverlayTimeFormatType::kHHMMSS_AMPM;
      } else if (!g_strcmp0 (g_value_get_string (value), "HHMM_24HR")) {
        date_entry->time_format = OverlayTimeFormatType::kHHMM_24HR;
      } else if (!g_strcmp0 (g_value_get_string (value), "HHMM_AMPM")) {
        date_entry->time_format = OverlayTimeFormatType::kHHMM_AMPM;
      } else {
        GST_ERROR ("Unsupported time format %s", g_value_get_string (value));
        return FALSE;
      }
      time_valid = TRUE;
    }

    if (!g_strcmp0 (name, "color")) {
      if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
        date_entry->color = g_value_get_uint (value);
        color_set = TRUE;
      }
      if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
        date_entry->color = (guint)g_value_get_int (value);
        color_set = TRUE;
      }
    }

    if (!g_strcmp0 (name, "dest-rect") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 4) {
      date_entry->dest_rect.x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      date_entry->dest_rect.y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      date_entry->dest_rect.w =
        g_value_get_int (gst_value_array_get_value (value, 2));
      date_entry->dest_rect.h =
        g_value_get_int (gst_value_array_get_value (value, 3));
    }
  }

  entry_valid = date_valid && time_valid;

  if (!color_set && entry_valid && !entry_exist) {
    date_entry->color = DEFAULT_PROP_OVERLAY_DATE_COLOR;
  }

  return entry_valid;
}

/**
 * gst_overlay_set_simg_overlay:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * This function parses user overlay configuration. Function is called when
 * overlay is configured by GST property.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_simg_overlay (GstOverlayUser * entry, GstStructure * structure,
  gboolean entry_exist)
{
  GstOverlayUsrSImg * simg_entry = (GstOverlayUsrSImg *) entry;
  gboolean entry_valid = FALSE;
  gboolean image_valid = FALSE;
  gboolean resolution_valid = FALSE;

  for (gint idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);

    const GValue *value = NULL;
    value = gst_structure_get_value (structure, name);

    if (!g_strcmp0 (name, "image") && G_VALUE_HOLDS (value, G_TYPE_STRING)) {
      simg_entry->img_file = g_strdup (g_value_get_string (value));
      if (!strlen (simg_entry->img_file)) {
        GST_INFO ("String is empty. Stop overlay if exist");
        break;
      }

      if (!g_file_test (simg_entry->img_file, G_FILE_TEST_IS_REGULAR)) {
        GST_INFO ("File %s does not exist", simg_entry->img_file);
        break;
      }

      // free previous buffer in case of reconfiguration
      if (entry_exist && simg_entry->img_buffer) {
        free (simg_entry->img_buffer);
        simg_entry->img_buffer = NULL;
        simg_entry->img_size = 0;
      }

      GError *error = NULL;
      gboolean ret = g_file_get_contents (simg_entry->img_file,
          &simg_entry->img_buffer, &simg_entry->img_size, &error);
      if (!ret) {
        GST_INFO ("Failed to get image file content, error: %s!",
            GST_STR_NULL (error->message));
        g_clear_error (&error);
        break;
      }
      image_valid = TRUE;
    }

    if (!g_strcmp0 (name, "resolution") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 2) {
      simg_entry->img_width =
        g_value_get_int (gst_value_array_get_value (value, 0));
      simg_entry->img_height =
        g_value_get_int (gst_value_array_get_value (value, 1));
      if (simg_entry->img_width == 0 || simg_entry->img_height == 0) {
        GST_INFO ("Invalid image resolution %dx%d!", simg_entry->img_width,
            simg_entry->img_height);
        break;
      }
      resolution_valid = TRUE;
    }

    if (!g_strcmp0 (name, "dest-rect") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 4) {
      simg_entry->dest_rect.x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      simg_entry->dest_rect.y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      simg_entry->dest_rect.w =
        g_value_get_int (gst_value_array_get_value (value, 2));
      simg_entry->dest_rect.h =
        g_value_get_int (gst_value_array_get_value (value, 3));
    }
  }

  entry_valid = image_valid && resolution_valid;

  if (!entry_valid && !entry_exist) {
    // Clean up if entry is not valid and does not exist. If entry exists but
    // it is not valid than entry will be stoped and release handle will take
    // care of cleaning up.
    if (simg_entry->img_file) {
      free (simg_entry->img_file);
    }
    if (simg_entry->img_buffer) {
      free (simg_entry->img_buffer);
    }
  }

  return entry_valid;
}

/**
 * gst_overlay_set_bbox_overlay:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * This function parses user overlay configuration. Function is called when
 * overlay is configured by GST property.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_bbox_overlay (GstOverlayUser * entry, GstStructure * structure,
  gboolean entry_exist)
{
  GstOverlayUsrBBox * bbox_entry = (GstOverlayUsrBBox *) entry;
  gboolean color_set = FALSE;
  gboolean entry_valid = FALSE;
  gboolean bbox_valid = FALSE;
  gboolean label_valid = FALSE;

  for (gint idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);

    const GValue *value = NULL;
    value = gst_structure_get_value (structure, name);

    if (!g_strcmp0 (name, "bbox") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY) &&
        gst_value_array_get_size (value) == 4) {
      bbox_entry->boundind_box.x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      bbox_entry->boundind_box.y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      bbox_entry->boundind_box.w =
        g_value_get_int (gst_value_array_get_value (value, 2));
      bbox_entry->boundind_box.h =
        g_value_get_int (gst_value_array_get_value (value, 3));
      bbox_valid = TRUE;
    }

    if (!g_strcmp0 (name, "label") && G_VALUE_HOLDS (value, G_TYPE_STRING)) {
      bbox_entry->label = g_strdup (g_value_get_string (value));
      if (strlen (bbox_entry->label) > 0) {
        label_valid = TRUE;
      } else {
        GST_INFO ("String is empty. Stop overlay if exist");
        free (bbox_entry->label);
        return FALSE;
      }
    }

    if (!g_strcmp0 (name, "color")) {
      if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
        bbox_entry->color = g_value_get_uint (value);
        color_set = TRUE;
      }
      if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
        bbox_entry->color = (guint)g_value_get_int (value);
        color_set = TRUE;
      }
    }
  }

  entry_valid = bbox_valid && label_valid;

  if (!color_set && entry_valid && !entry_exist) {
    bbox_entry->color = DEFAULT_PROP_OVERLAY_BBOX_COLOR;
  }

  return entry_valid;
}

/**
 * gst_overlay_set_mask_overlay:
 * @entry: result of parsed input is stored here
 * @structure: user input
 * @entry_exist: hint if entry exist or new entry. This is helpfull when
 *               some default values are needed to be set.
 *
 * This function parses user overlay configuration. Function is called when
 * overlay is configured by GST property.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_mask_overlay (GstOverlayUser * entry, GstStructure * structure,
  gboolean entry_exist)
{
  GstOverlayUsrMask * mask_entry = (GstOverlayUsrMask *) entry;
  gboolean color_set = FALSE;
  gboolean entry_valid = FALSE;
  gboolean circle_valid = FALSE;
  gboolean rectangle_valid = FALSE;
  gboolean dest_rect_valid = FALSE;
  gboolean inverse = FALSE;

  for (gint idx = 0; idx < gst_structure_n_fields (structure); ++idx) {
    const gchar *name = gst_structure_nth_field_name (structure, idx);

    const GValue *value = NULL;
    value = gst_structure_get_value (structure, name);

    if (!g_strcmp0 (name, "circle") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 3) {
      mask_entry->circle.center_x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      mask_entry->circle.center_y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      mask_entry->circle.radius =
        g_value_get_int (gst_value_array_get_value (value, 2));
      circle_valid = TRUE;
    }

    if (!g_strcmp0 (name, "rectangle") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 4) {
      mask_entry->rectangle.start_x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      mask_entry->rectangle.start_y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      mask_entry->rectangle.width =
        g_value_get_int (gst_value_array_get_value (value, 2));
      mask_entry->rectangle.height =
        g_value_get_int (gst_value_array_get_value (value, 3));
      rectangle_valid = TRUE;
    }

    if (!g_strcmp0 (name, "inverse") && G_VALUE_HOLDS (value, G_TYPE_BOOLEAN)) {
      inverse = g_value_get_boolean (value);
    }

    if (!g_strcmp0 (name, "color")) {
      if (G_VALUE_HOLDS (value, G_TYPE_UINT)) {
        mask_entry->color = g_value_get_uint (value);
        color_set = TRUE;
      }
      if (G_VALUE_HOLDS (value, G_TYPE_INT)) {
        mask_entry->color = (guint)g_value_get_int (value);
        color_set = TRUE;
      }
    }

    if (!g_strcmp0 (name, "dest-rect") && G_VALUE_HOLDS (value, GST_TYPE_ARRAY)
         && gst_value_array_get_size (value) == 4) {
      mask_entry->dest_rect.x =
        g_value_get_int (gst_value_array_get_value (value, 0));
      mask_entry->dest_rect.y =
        g_value_get_int (gst_value_array_get_value (value, 1));
      mask_entry->dest_rect.w =
        g_value_get_int (gst_value_array_get_value (value, 2));
      mask_entry->dest_rect.h =
        g_value_get_int (gst_value_array_get_value (value, 3));
      dest_rect_valid = TRUE;
    }
  }

  if (circle_valid && rectangle_valid) {
    GST_INFO ("circle and rectangle cannot be set in the same time");
    return FALSE;
  }

  entry_valid = (circle_valid || rectangle_valid) && dest_rect_valid;

  if (entry_valid) {
    if (circle_valid && inverse) {
      mask_entry->type = OverlayPrivacyMaskType::kInverseCircle;
    } else if (circle_valid) {
      mask_entry->type = OverlayPrivacyMaskType::kCircle;
    } else if (rectangle_valid && inverse) {
      mask_entry->type = OverlayPrivacyMaskType::kInverseRectangle;
    } else if (rectangle_valid) {
      mask_entry->type = OverlayPrivacyMaskType::kRectangle;
    } else {
      GST_INFO ("Error cannot find privacy mask type!");
      return FALSE;
    }

    if (!color_set && !entry_exist) {
      mask_entry->color = DEFAULT_PROP_OVERLAY_MASK_COLOR;
    }
  }

  return entry_valid;
}

/**
 * gst_overlay_compare_overlay_id:
 * @a: a value
 * @b: a value to compare with
 *
 * Compares two user overlay instance ids.
 *
 * Return 0 if match.
 */
static gint
gst_overlay_compare_overlay_id (gconstpointer a, gconstpointer b, gpointer)
{
  return g_strcmp0 (((const GstOverlayUser *) a)->user_id,
      ((const GstOverlayUser *) b)->user_id);
}

/**
 * gst_overlay_set_user_overlay:
 * @gst_overlay: context
 * @user_ov: pointer to GSequence of user overlays of the same type
 * @entry_size: size of configuration structure of the overlay type
 * @set_func: function which set overlay type specific parameters
 * @value: input configuration which comes from GST property
 *
 * The generic setter for all user overlays. This function parse input string
 * to GstStructure. Check if overlay instance already exists. Creates new if
 * does not exist otherwise updates existing one. If mandatory parameters are
 * not provided overlay instance is distoried. set_func is use to set
 * overlay specific parameters.
 */
static void
gst_overlay_set_user_overlay (GstOverlay *gst_overlay, GSequence * user_ov,
    guint entry_size, GstOverlaySetFunc set_func, const GValue * value)
{
  const gchar *input = g_value_get_string (value);

  if (!input) {
    GST_WARNING ("Empty input. Default value or invalid user input.");
    return;
  }

  GValue gvalue = G_VALUE_INIT;
  g_value_init (&gvalue, GST_TYPE_STRUCTURE);

  gboolean success = gst_value_deserialize (&gvalue, input);
  if (!success) {
    GST_WARNING ("Failed to deserialize text overlay input <%s>", input);
    return;
  }

  GstStructure *structure = GST_STRUCTURE (g_value_dup_boxed (&gvalue));
  g_value_unset (&gvalue);

  gboolean entry_valid = FALSE;
  gboolean entry_exist = FALSE;

  gchar *ov_id = (gchar *) gst_structure_get_name(structure);

  g_mutex_lock (&gst_overlay->lock);

  GstOverlayUser * entry = NULL;
  GstOverlayUser lookup;
  lookup.user_id = ov_id;

  GSequenceIter * iter = g_sequence_lookup (user_ov, &lookup,
      gst_overlay_compare_overlay_id, NULL);
  if (iter) {
    entry = (GstOverlayUser *) g_sequence_get (iter);
    entry_exist = TRUE;
  } else {
    entry = (GstOverlayUser *) calloc (1, entry_size);
    if (!entry) {
      GST_ERROR("failed to allocate memory for new entry");
      g_mutex_unlock (&gst_overlay->lock);
      return;
    }
    entry->user_data = gst_overlay;
  }

  entry_valid = set_func (entry, structure, entry_exist);

  gst_structure_free (structure);

  if (entry_valid && entry_exist) {
    entry->is_applied = FALSE;
  } else if (entry_valid) {
    entry->user_id = g_strdup (ov_id);
    g_sequence_insert_sorted (user_ov, entry,
                              gst_overlay_compare_overlay_id, NULL);
  } else if (entry_exist) {
    g_sequence_remove (iter);
  } else {
    free (entry);
  }

  g_mutex_unlock (&gst_overlay->lock);
}

/**
 * gst_overlay_text_overlay_to_string:
 * @data: user text overlay entry of GstOverlayUsrText type
 * @user_data: output string of GstOverlayString type
 *
 * Converts text overlay configuration to string.
 */
static void
gst_overlay_text_overlay_to_string (gpointer data, gpointer user_data)
{
  GstOverlayUsrText * ov_data = (GstOverlayUsrText *) data;
  GstOverlayString * output = (GstOverlayString *) user_data;

  gint size = GST_OVERLAY_TEXT_STRING_SIZE + strlen (ov_data->base.user_id) +
    strlen (ov_data->text);
  gchar * tmp = (gchar *) malloc(size);
  if (!tmp) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    return;
  }

  gint ret = snprintf (tmp, size,
    "%s, text=\"%s\", color=0x%x, dest-rect=<%d, %d, %d, %d>; ",
    ov_data->base.user_id, ov_data->text, ov_data->color, ov_data->dest_rect.x,
    ov_data->dest_rect.y, ov_data->dest_rect.w, ov_data->dest_rect.h);
  if (ret < 0 || ret >= size) {
    GST_ERROR ("%s: String size %d exceed size %d", __func__, ret, size);
    free (tmp);
    return;
  }

  if (output->capacity < (strlen (output->string) + strlen (tmp))) {
    size = (strlen (output->string) + strlen (tmp)) * 2;
    output->string = (gchar *) realloc(output->string, size);
    if (!output->string) {
      GST_ERROR ("%s: Failed to reallocate memory. Size %d", __func__, size);
      free (tmp);
      return;
    }
    output->capacity = size;
  }

  g_strlcat (output->string, tmp, output->capacity);

  free (tmp);
}

/**
 * gst_overlay_date_overlay_to_string:
 * @data: user date overlay entry of GstOverlayUsrDate type
 * @user_data: output string of GstOverlayString type
 *
 * Converts date overlay configuration to string.
 */
static void
gst_overlay_date_overlay_to_string (gpointer data, gpointer user_data)
{
  GstOverlayUsrDate * ov_data = (GstOverlayUsrDate *) data;
  GstOverlayString * output = (GstOverlayString *) user_data;

  gint size = GST_OVERLAY_DATE_STRING_SIZE + strlen (ov_data->base.user_id);
  gchar * tmp = (gchar *) malloc (size);
  if (!tmp) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    return;
  }

  gchar * date_format;
  switch (ov_data->date_format) {
    case OverlayDateFormatType::kYYYYMMDD:
      date_format = (gchar *)"YYYYMMDD";
      break;
    case OverlayDateFormatType::kMMDDYYYY:
      date_format = (gchar *)"MMDDYYYY";
      break;
    default:
      GST_ERROR ("Error unsupported date format %d", (gint)ov_data->date_format);
      free (tmp);
      return;
  }

  gchar * time_format;
  switch (ov_data->time_format) {
    case OverlayTimeFormatType::kHHMMSS_24HR:
      time_format = (gchar *)"HHMMSS_24HR";
      break;
    case OverlayTimeFormatType::kHHMMSS_AMPM:
      time_format = (gchar *)"HHMMSS_AMPM";
      break;
    case OverlayTimeFormatType::kHHMM_24HR:
      time_format = (gchar *)"HHMM_24HR";
      break;
    case OverlayTimeFormatType::kHHMM_AMPM:
      time_format = (gchar *)"HHMM_AMPM";
      break;
    default:
      GST_ERROR ("Error unsupported time format %d", (gint)ov_data->time_format);
      free (tmp);
      return;
  }

  gint ret = snprintf (tmp, size,
    "%s, date-format=%s, time-format=%s, color=0x%x, dest-rect=<%d, %d, %d, %d>; ",
    ov_data->base.user_id, date_format, time_format, ov_data->color,
    ov_data->dest_rect.x, ov_data->dest_rect.y, ov_data->dest_rect.w,
    ov_data->dest_rect.h);
  if (ret < 0 || ret >= size) {
    GST_ERROR ("%s: String size %d exceed size %d", __func__, ret, size);
    free (tmp);
    return;
  }

  if (output->capacity < (strlen (output->string) + strlen (tmp))) {
    size = (strlen (output->string) + strlen (tmp)) * 2;
    output->string = (gchar *) realloc(output->string, size);
    if (!output->string) {
      GST_ERROR ("%s: Failed to reallocate memory. Size %d", __func__, size);
      free (tmp);
      return;
    }
    output->capacity = size;
  }

  g_strlcat (output->string, tmp, output->capacity);

  free (tmp);
}

/**
 * gst_overlay_simg_overlay_to_string:
 * @data: user static image overlay entry of GstOverlayUsrSImg type
 * @user_data: output string of GstOverlayString type
 *
 * Converts static image overlay configuration to string.
 */
static void
gst_overlay_simg_overlay_to_string (gpointer data, gpointer user_data)
{
  GstOverlayUsrSImg * ov_data = (GstOverlayUsrSImg *) data;
  GstOverlayString * output = (GstOverlayString *) user_data;

  gint size = GST_OVERLAY_SIMG_STRING_SIZE + strlen (ov_data->base.user_id) +
      strlen (ov_data->img_file);
  gchar * tmp = (gchar *) malloc(size);
  if (!tmp) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    return;
  }

  gint ret = snprintf (tmp, size,
    "%s, image=\"%s\", resolution=<%d, %d>, dest-rect=<%d, %d, %d, %d>; ",
    ov_data->base.user_id, ov_data->img_file, ov_data->img_width,
    ov_data->img_height, ov_data->dest_rect.x, ov_data->dest_rect.y,
    ov_data->dest_rect.w, ov_data->dest_rect.h);
  if (ret < 0 || ret >= size) {
    GST_ERROR ("%s: String size %d exceed size %d", __func__, ret, size);
    free (tmp);
    return;
  }

  if (output->capacity < (strlen (output->string) + strlen (tmp))) {
    size = (strlen (output->string) + strlen (tmp)) * 2;
    output->string = (gchar *) realloc(output->string, size);
    if (!output->string) {
      GST_ERROR ("%s: Failed to reallocate memory. Size %d", __func__, size);
      free (tmp);
      return;
    }
    output->capacity = size;
  }

  g_strlcat (output->string, tmp, output->capacity);

  free (tmp);
}

/**
 * gst_overlay_bbox_overlay_to_string:
 * @data: user text overlay entry of GstOverlayUsrBBox type
 * @user_data: output string of GstOverlayString type
 *
 * Converts text overlay configuration to string.
 */
static void
gst_overlay_bbox_overlay_to_string (gpointer data, gpointer user_data)
{
  GstOverlayUsrBBox * ov_data = (GstOverlayUsrBBox *) data;
  GstOverlayString * output = (GstOverlayString *) user_data;

  gint size = GST_OVERLAY_BBOX_STRING_SIZE + strlen (ov_data->base.user_id) +
    strlen (ov_data->label);
  gchar * tmp = (gchar *) malloc(size);
  if (!tmp) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    return;
  }

  gint ret = snprintf (tmp, size,
    "%s, bbox=<%d, %d, %d, %d>, label=\"%s\", color=0x%x; ",
    ov_data->base.user_id, ov_data->boundind_box.x, ov_data->boundind_box.y,
    ov_data->boundind_box.w, ov_data->boundind_box.h, ov_data->label,
    ov_data->color);
  if (ret < 0 || ret >= size) {
    GST_ERROR ("%s: String size %d exceed size %d", __func__, ret, size);
    free (tmp);
    return;
  }

  if (output->capacity < (strlen (output->string) + strlen (tmp))) {
    size = (strlen (output->string) + strlen (tmp)) * 2;
    output->string = (gchar *) realloc(output->string, size);
    if (!output->string) {
      GST_ERROR ("%s: Failed to reallocate memory. Size %d", __func__, size);
      free (tmp);
      return;
    }
    output->capacity = size;
  }

  g_strlcat (output->string, tmp, output->capacity);

  free (tmp);
}

/**
 * gst_overlay_mask_overlay_to_string:
 * @data: user text overlay entry of GstOverlayUsrMask type
 * @user_data: output string of GstOverlayString type
 *
 * Converts privacy mask overlay configuration to string.
 */
static void
gst_overlay_mask_overlay_to_string (gpointer data, gpointer user_data)
{
  GstOverlayUsrMask * ov_data = (GstOverlayUsrMask *) data;
  GstOverlayString * output = (GstOverlayString *) user_data;

  gint size = GST_OVERLAY_MASK_STRING_SIZE + strlen (ov_data->base.user_id);
  gchar * tmp = (gchar *) malloc(size);
  if (!tmp) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    return;
  }

  gint ret;
  if (ov_data->type == OverlayPrivacyMaskType::kRectangle ||
      ov_data->type == OverlayPrivacyMaskType::kInverseRectangle) {
    ret = snprintf (tmp, size,
        "%s, rectangle=<%d, %d, %d, %d>, inverse=%s, color=0x%x, dest-rect=<%d, %d, %d, %d>; ",
        ov_data->base.user_id, ov_data->rectangle.start_x,
        ov_data->rectangle.start_y, ov_data->rectangle.width,
        ov_data->rectangle.height,
        ov_data->type == OverlayPrivacyMaskType::kRectangle ? "false" : "true",
        ov_data->color, ov_data->dest_rect.x, ov_data->dest_rect.y,
        ov_data->dest_rect.w, ov_data->dest_rect.h);
  } else {
    ret = snprintf (tmp, size,
        "%s, circle=<%d, %d, %d>, inverse=%s, color=0x%x, dest-rect=<%d, %d, %d, %d>; ",
        ov_data->base.user_id, ov_data->circle.center_x,
        ov_data->circle.center_y, ov_data->circle.radius,
        ov_data->type == OverlayPrivacyMaskType::kRectangle ? "false" : "true",
        ov_data->color, ov_data->dest_rect.x, ov_data->dest_rect.y,
        ov_data->dest_rect.w, ov_data->dest_rect.h);
  }

  if (ret < 0 || ret >= size) {
    GST_ERROR ("%s: String size %d exceed size %d", __func__, ret, size);
    free (tmp);
    return;
  }

  if (output->capacity < (strlen (output->string) + strlen (tmp))) {
    size = (strlen (output->string) + strlen (tmp)) * 2;
    output->string = (gchar *) realloc(output->string, size);
    if (!output->string) {
      GST_ERROR ("%s: Failed to reallocate memory. Size %d", __func__, size);
      free (tmp);
      return;
    }
    output->capacity = size;
  }

  g_strlcat (output->string, tmp, output->capacity);

  free (tmp);
}

/**
 * gst_overlay_get_user_overlay:
 * @gst_overlay: context
 * @value: output value
 * @user_ov: list of overlay setting of one type
 * @get_func: parameter features
 *
 * The generic getter for all user overlay setting. This function iterate
 * all overlay instances provided by user_ov paramter and converts it to
 * string by provided get_func function.
 */
static void
gst_overlay_get_user_overlay (GstOverlay *gst_overlay, GValue * value,
    GSequence * user_ov, GstOverlayGetFunc get_func)
{
  g_mutex_lock (&gst_overlay->lock);

  GstOverlayString output;
  output.capacity = GST_OVERLAY_TO_STRING_SIZE;
  output.string = (gchar *) malloc (GST_OVERLAY_TO_STRING_SIZE);
  if (!output.string) {
    GST_ERROR ("%s: failed to allocate memory", __func__);
    g_mutex_unlock (&gst_overlay->lock);
    return;
  }
  output.string[0] = '\0';

  g_sequence_foreach (user_ov, get_func, &output);
  g_value_set_string (value, output.string);
  free (output.string);

  g_mutex_unlock (&gst_overlay->lock);
}

/**
 * gst_overlay_get_property:
 * @object: gst overlay object
 * @prop_id: GST property id
 * @value: value of GST property
 * @pspec: parameter features
 *
 * The generic setter for all properties of this type.
 */
static void
gst_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);
  const gchar *propname = g_param_spec_get_name (pspec);
  GstState state = GST_STATE (gst_overlay);

  if (!OVERLAY_IS_PROPERTY_MUTABLE_IN_CURRENT_STATE(pspec, state)) {
    GST_WARNING ("Property '%s' change not supported in %s state!",
        propname, gst_element_state_get_name (state));
    return;
  }

  GST_OBJECT_LOCK (gst_overlay);
  switch (prop_id) {
    case PROP_OVERLAY_TEXT:
      gst_overlay_set_user_overlay (gst_overlay, gst_overlay->usr_text,
        sizeof (GstOverlayUsrText), gst_overlay_set_text_overlay, value);
      break;
    case PROP_OVERLAY_DATE:
      gst_overlay_set_user_overlay (gst_overlay, gst_overlay->usr_date,
        sizeof (GstOverlayUsrDate), gst_overlay_set_date_overlay, value);
      break;
    case PROP_OVERLAY_SIMG:
      gst_overlay_set_user_overlay (gst_overlay, gst_overlay->usr_simg,
        sizeof (GstOverlayUsrSImg), gst_overlay_set_simg_overlay, value);
      break;
    case PROP_OVERLAY_BBOX:
      gst_overlay_set_user_overlay (gst_overlay, gst_overlay->usr_bbox,
        sizeof (GstOverlayUsrBBox), gst_overlay_set_bbox_overlay, value);
      break;
    case PROP_OVERLAY_MASK:
      gst_overlay_set_user_overlay (gst_overlay, gst_overlay->usr_mask,
        sizeof (GstOverlayUsrMask), gst_overlay_set_mask_overlay, value);
      break;
    case PROP_OVERLAY_META_COLOR:
      gst_overlay->meta_color = g_value_get_boolean (value);
    case PROP_OVERLAY_BBOX_COLOR:
      gst_overlay->bbox_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_DATE_COLOR:
      gst_overlay->date_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_TEXT_COLOR:
      gst_overlay->text_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_POSE_COLOR:
      gst_overlay->pose_color = g_value_get_uint (value);
      break;
    case PROP_OVERLAY_TEXT_DEST_RECT:
      if (gst_value_array_get_size(value) != 4) {
        GST_DEBUG_OBJECT(gst_overlay,
          "dest-rect is not set. Use default values.");
        break;
      }
      gst_overlay->text_dest_rect.x =
          g_value_get_int(gst_value_array_get_value(value, 0));
      gst_overlay->text_dest_rect.y =
          g_value_get_int(gst_value_array_get_value(value, 1));
      gst_overlay->text_dest_rect.w =
          g_value_get_int(gst_value_array_get_value(value, 2));
      gst_overlay->text_dest_rect.h =
          g_value_get_int(gst_value_array_get_value(value, 3));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (gst_overlay);
}

/**
 * gst_overlay_get_property:
 * @object: gst overlay object
 * @prop_id: GST property id
 * @value: output value
 * @pspec: parameter features
 *
 * The generic getter for all properties of this type.
 */
static void
gst_overlay_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);

  GST_OBJECT_LOCK (gst_overlay);
  switch (prop_id) {
    case PROP_OVERLAY_TEXT:
      gst_overlay_get_user_overlay (gst_overlay, value,
        gst_overlay->usr_text, gst_overlay_text_overlay_to_string);
      break;
    case PROP_OVERLAY_DATE:
      gst_overlay_get_user_overlay (gst_overlay, value,
        gst_overlay->usr_date, gst_overlay_date_overlay_to_string);
      break;
    case PROP_OVERLAY_SIMG:
      gst_overlay_get_user_overlay (gst_overlay, value,
        gst_overlay->usr_simg, gst_overlay_simg_overlay_to_string);
      break;
    case PROP_OVERLAY_BBOX:
      gst_overlay_get_user_overlay (gst_overlay, value,
        gst_overlay->usr_bbox, gst_overlay_bbox_overlay_to_string);
      break;
    case PROP_OVERLAY_MASK:
      gst_overlay_get_user_overlay (gst_overlay, value,
        gst_overlay->usr_mask, gst_overlay_mask_overlay_to_string);
      break;
    case PROP_OVERLAY_META_COLOR:
      g_value_set_boolean (value, gst_overlay->meta_color);
      break;
    case PROP_OVERLAY_BBOX_COLOR:
      g_value_set_uint (value, gst_overlay->bbox_color);
      break;
    case PROP_OVERLAY_DATE_COLOR:
      g_value_set_uint (value, gst_overlay->date_color);
      break;
    case PROP_OVERLAY_TEXT_COLOR:
      g_value_set_uint (value, gst_overlay->text_color);
      break;
    case PROP_OVERLAY_POSE_COLOR:
      g_value_set_uint (value, gst_overlay->pose_color);
      break;
    case PROP_OVERLAY_TEXT_DEST_RECT:
    {
      GValue val = G_VALUE_INIT;
      g_value_init (&val, G_TYPE_INT);

      g_value_set_int (&val, gst_overlay->text_dest_rect.x);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, gst_overlay->text_dest_rect.y);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, gst_overlay->text_dest_rect.w);
      gst_value_array_append_value (value, &val);

      g_value_set_int (&val, gst_overlay->text_dest_rect.h);
      gst_value_array_append_value (value, &val);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (gst_overlay);
}

/**
 * gst_overlay_finalize:
 * @object: gst overlay object
 *
 * GST object finalize handler.
 */
static void
gst_overlay_finalize (GObject * object)
{
  GstOverlay *gst_overlay = GST_OVERLAY (object);

  if (gst_overlay->overlay) {
    g_sequence_foreach (gst_overlay->bbox_id, gst_overlay_destroy_overlay_item,
        gst_overlay->overlay);
    g_sequence_free (gst_overlay->bbox_id);

    g_sequence_foreach (gst_overlay->simg_id, gst_overlay_destroy_overlay_item,
        gst_overlay->overlay);
    g_sequence_free (gst_overlay->simg_id);

    g_sequence_foreach (gst_overlay->text_id, gst_overlay_destroy_overlay_item,
        gst_overlay->overlay);
    g_sequence_free (gst_overlay->text_id);

    g_sequence_foreach (gst_overlay->pose_id, gst_overlay_destroy_overlay_item,
        gst_overlay->overlay);
    g_sequence_free (gst_overlay->pose_id);

    g_sequence_free (gst_overlay->usr_text);
    g_sequence_free (gst_overlay->usr_date);
    g_sequence_free (gst_overlay->usr_simg);
    g_sequence_free (gst_overlay->usr_bbox);
    g_sequence_free (gst_overlay->usr_mask);

    delete (gst_overlay->overlay);
    gst_overlay->overlay = nullptr;
  }

  g_mutex_clear (&gst_overlay->lock);

  G_OBJECT_CLASS (parent_class)->finalize (G_OBJECT (gst_overlay));
}

/**
 * gst_overlay_set_info:
 * @filter: gst overlay object
 * @in: negotiated sink pad capabilites
 * @ininfo: Information describing input image properties
 * @out: negotiated source pad capabilites
 * @outinfo: Information describing output image properties
 *
 * Function to be called with the negotiated caps and video infos.
 *
 * Return true if succeed.
 */
static gboolean
gst_overlay_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * ininfo, GstCaps * out, GstVideoInfo * outinfo)
{
  GstOverlay *gst_overlay = GST_OVERLAY (filter);
  TargetBufferFormat  new_format;

  GST_OVERLAY_UNUSED(in);
  GST_OVERLAY_UNUSED(out);
  GST_OVERLAY_UNUSED(outinfo);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);

  gst_overlay->width = GST_VIDEO_INFO_WIDTH (ininfo);
  gst_overlay->height = GST_VIDEO_INFO_HEIGHT (ininfo);

  switch (GST_VIDEO_INFO_FORMAT(ininfo)) { // GstVideoFormat
    case GST_VIDEO_FORMAT_NV12:
      new_format = TargetBufferFormat::kYUVNV12;
      break;
    case GST_VIDEO_FORMAT_NV21:
      new_format = TargetBufferFormat::kYUVNV21;
      break;
    default:
      GST_ERROR_OBJECT (gst_overlay, "Unhandled gst format: %d",
        GST_VIDEO_INFO_FORMAT (ininfo));
      return FALSE;
  }

  if (gst_overlay->overlay && gst_overlay->format == new_format) {
    GST_DEBUG_OBJECT (gst_overlay, "Overlay already initialized");
    return TRUE;
  }

  if (gst_overlay->overlay) {
    delete (gst_overlay->overlay);
  }

  gst_overlay->format = new_format;
  gst_overlay->overlay = new Overlay();

  int32_t ret = gst_overlay->overlay->Init (gst_overlay->format);
  if (ret != 0) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay init failed! Format: %u",
        (guint)gst_overlay->format);
    delete (gst_overlay->overlay);
    gst_overlay->overlay = nullptr;
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_overlay_set_info:
 * @filter: gst overlay object
 * @frame: GST video buffer
 *
 * Apply all overlay items from machine learning metadata and user provided
 * overlays in video frame in place.
 *
 * Return GST_FLOW_OK if succeed otherwise GST_FLOW_ERROR.
 */
static GstFlowReturn
gst_overlay_transform_frame_ip (GstVideoFilter *filter, GstVideoFrame *frame)
{
  GstOverlay *gst_overlay = GST_OVERLAY_CAST (filter);
  gboolean res = TRUE;

  if (!gst_overlay->overlay) {
    GST_ERROR_OBJECT (gst_overlay, "failed: overlay not initialized");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_detection_meta (frame->buffer),
                            gst_overlay_apply_ml_bbox_item,
                            gst_overlay->bbox_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply bbox item list failed!");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_segmentation_meta (frame->buffer),
                            gst_overlay_apply_ml_simg_item,
                            gst_overlay->simg_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply image item list failed!");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_classification_meta (frame->buffer),
                            gst_overlay_apply_ml_text_item,
                            gst_overlay->text_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay,
        "Overlay apply classification item list failed!");
    return GST_FLOW_ERROR;
  }

  res = gst_overlay_apply_item_list (gst_overlay,
                            gst_buffer_get_posenet_meta (frame->buffer),
                            gst_overlay_apply_ml_pose_item,
                            gst_overlay->pose_id);
  if (!res) {
    GST_ERROR_OBJECT (gst_overlay, "Overlay apply pose item list failed!");
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&gst_overlay->lock);

  g_sequence_foreach (gst_overlay->usr_text,
                      gst_overlay_apply_user_text_item,
                      gst_overlay);

  g_sequence_foreach (gst_overlay->usr_date,
                      gst_overlay_apply_user_date_item,
                      gst_overlay);

  g_sequence_foreach (gst_overlay->usr_simg,
                      gst_overlay_apply_user_simg_item,
                      gst_overlay);

  g_sequence_foreach (gst_overlay->usr_bbox,
                      gst_overlay_apply_user_bbox_item,
                      gst_overlay);

  g_sequence_foreach (gst_overlay->usr_mask,
                      gst_overlay_apply_user_mask_item,
                      gst_overlay);

  g_mutex_unlock (&gst_overlay->lock);

  if (!g_sequence_is_empty (gst_overlay->bbox_id) ||
      !g_sequence_is_empty (gst_overlay->simg_id) ||
      !g_sequence_is_empty (gst_overlay->text_id) ||
      !g_sequence_is_empty (gst_overlay->pose_id) ||
      !g_sequence_is_empty (gst_overlay->usr_text) ||
      !g_sequence_is_empty (gst_overlay->usr_date) ||
      !g_sequence_is_empty (gst_overlay->usr_simg) ||
      !g_sequence_is_empty (gst_overlay->usr_bbox) ||
      !g_sequence_is_empty (gst_overlay->usr_mask)) {
    res = gst_overlay_apply_overlay (gst_overlay, frame);
    if (!res) {
      GST_ERROR_OBJECT (gst_overlay, "Overlay apply failed!");
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

/**
 * gst_overlay_free_user_overlay_entry:
 * @ptr: GstOverlayUser
 *
 * Disable overlay item and free all user overlay common data. All resources
 * freed in this function are allocated in gst_overlay_set_user_overlay().
 */
static void
gst_overlay_free_user_overlay_entry (gpointer ptr)
{
  if (ptr) {
    GstOverlayUser * entry = (GstOverlayUser *) ptr;
    GstOverlay * gst_overlay = (GstOverlay *) entry->user_data;
    if (entry->item_id && gst_overlay && gst_overlay->overlay) {
      gst_overlay_destroy_overlay_item (&entry->item_id, gst_overlay->overlay);
    }
    free (entry->user_id);
    free (entry);
  }
}

/**
 * gst_overlay_free_user_text_entry:
 * @ptr: GstOverlayUsrText
 *
 * Free text user overlay data.
 */
static void
gst_overlay_free_user_text_entry (gpointer ptr)
{
  if (ptr) {
    GstOverlayUsrText * entry = (GstOverlayUsrText *) ptr;
    free (entry->text);
    gst_overlay_free_user_overlay_entry (ptr);
  }
}

/**
 * gst_overlay_free_user_simg_entry:
 * @ptr: GstOverlayUsrSImg
 *
 * Free static image user overlay data.
 */
static void
gst_overlay_free_user_simg_entry (gpointer ptr)
{
  if (ptr) {
    GstOverlayUsrSImg * entry = (GstOverlayUsrSImg *) ptr;
    free (entry->img_file);
    free (entry->img_buffer);
    gst_overlay_free_user_overlay_entry (ptr);
  }
}

/**
 * gst_overlay_free_user_bbox_entry:
 * @ptr: GstOverlayUsrBBox
 *
 * Free bounding box user overlay data.
 */
static void
gst_overlay_free_user_bbox_entry (gpointer ptr)
{
  if (ptr) {
    GstOverlayUsrBBox * entry = (GstOverlayUsrBBox *) ptr;
    free (entry->label);
    gst_overlay_free_user_overlay_entry (ptr);
  }
}

static void
gst_overlay_init (GstOverlay * gst_overlay)
{
  gst_overlay->overlay = nullptr;

  gst_overlay->bbox_id = g_sequence_new (free);
  gst_overlay->simg_id = g_sequence_new (free);
  gst_overlay->text_id = g_sequence_new (free);
  gst_overlay->pose_id = g_sequence_new (free);

  gst_overlay->usr_text = g_sequence_new (gst_overlay_free_user_text_entry);
  gst_overlay->usr_date = g_sequence_new (gst_overlay_free_user_overlay_entry);
  gst_overlay->usr_simg = g_sequence_new (gst_overlay_free_user_simg_entry);
  gst_overlay->usr_bbox = g_sequence_new (gst_overlay_free_user_bbox_entry);
  gst_overlay->usr_mask = g_sequence_new (gst_overlay_free_user_overlay_entry);

  gst_overlay->meta_color = DEFAULT_PROP_OVERLAY_META_COLOR;
  gst_overlay->bbox_color = DEFAULT_PROP_OVERLAY_BBOX_COLOR;
  gst_overlay->date_color = DEFAULT_PROP_OVERLAY_DATE_COLOR;
  gst_overlay->text_color = DEFAULT_PROP_OVERLAY_TEXT_COLOR;
  gst_overlay->pose_color = DEFAULT_PROP_OVERLAY_POSE_COLOR;
  gst_overlay->text_dest_rect.x = DEFAULT_PROP_DEST_RECT_X;
  gst_overlay->text_dest_rect.y = DEFAULT_PROP_DEST_RECT_Y;
  gst_overlay->text_dest_rect.w = DEFAULT_PROP_DEST_RECT_WIDTH;
  gst_overlay->text_dest_rect.h = DEFAULT_PROP_DEST_RECT_HEIGHT;

  g_mutex_init (&gst_overlay->lock);

  GST_DEBUG_CATEGORY_INIT (overlay_debug, "qtioverlay", 0, "QTI overlay");
}

static void
gst_overlay_class_init (GstOverlayClass * klass)
{
  GObjectClass *gobject            = G_OBJECT_CLASS (klass);
  GstElementClass *element         = GST_ELEMENT_CLASS (klass);
  GstVideoFilterClass *filter      = GST_VIDEO_FILTER_CLASS (klass);

  gobject->set_property = GST_DEBUG_FUNCPTR (gst_overlay_set_property);
  gobject->get_property = GST_DEBUG_FUNCPTR (gst_overlay_get_property);
  gobject->finalize     = GST_DEBUG_FUNCPTR (gst_overlay_finalize);

  g_object_class_install_property (gobject, PROP_OVERLAY_TEXT,
    g_param_spec_string ("overlay-text", "Text Overlay",
      "Renders text on top of video stream.",
      DEFAULT_PROP_OVERLAY_TEXT, static_cast <GParamFlags> (
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_OVERLAY_DATE,
    g_param_spec_string ("overlay-date", "Date Overlay",
      "Renders date and time on top of video stream.",
      DEFAULT_PROP_OVERLAY_DATE, static_cast <GParamFlags> (
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_OVERLAY_SIMG,
    g_param_spec_string ("overlay-simg", "Static Image Overlay",
      "Renders static image on top of video stream.",
      DEFAULT_PROP_OVERLAY_DATE, static_cast <GParamFlags> (
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_OVERLAY_BBOX,
    g_param_spec_string ("overlay-bbox", "Bounding Box Overlay",
      "Renders bounding box and label on top of video stream.",
      DEFAULT_PROP_OVERLAY_TEXT, static_cast <GParamFlags> (
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_OVERLAY_MASK,
    g_param_spec_string ("overlay-mask", "Privacy Mask Overlay",
      "Renders privacy mask on top of video stream.",
      DEFAULT_PROP_OVERLAY_TEXT, static_cast <GParamFlags> (
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING)));

  g_object_class_install_property (gobject, PROP_OVERLAY_META_COLOR,
    g_param_spec_boolean ("meta-color", "Meta color", "Bounding box overlay use meta data color",
      DEFAULT_PROP_OVERLAY_META_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_BBOX_COLOR,
    g_param_spec_uint ("bbox-color", "BBox color", "Bounding box overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_BBOX_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_DATE_COLOR,
    g_param_spec_uint ("date-color", "Date color", "Date overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_DATE_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_TEXT_COLOR,
    g_param_spec_uint ("text-color", "Text color", "Text overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_TEXT_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_POSE_COLOR,
    g_param_spec_uint ("pose-color", "Pose color", "Pose overlay color",
      0, G_MAXUINT, DEFAULT_PROP_OVERLAY_POSE_COLOR, static_cast<GParamFlags>(
        G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject, PROP_OVERLAY_TEXT_DEST_RECT,
      gst_param_spec_array ("dest-rect-ml-text",
          "Destination Rectangle for ML Detection overlay",
          "Destination rectangle params for ML Detection overlay. "
          "The Start-X, Start-Y , Width, Height of the destination rectangle "
          "format is <X, Y, WIDTH, HEIGHT>",
          g_param_spec_int ("coord", "Coordinate",
              "One of X, Y, Width, Height value.", 0, G_MAXINT, 0,
              static_cast <GParamFlags> (G_PARAM_WRITABLE |
                                         G_PARAM_STATIC_STRINGS)),
          static_cast <GParamFlags> (G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element, "QTI Overlay", "Overlay",
      "This plugin renders text, image, bounding box or graph on top of a "
      "video stream.", "QTI");

  gst_element_class_add_pad_template (element, gst_overlay_sink_template ());
  gst_element_class_add_pad_template (element, gst_overlay_src_template ());

  filter->set_info = GST_DEBUG_FUNCPTR (gst_overlay_set_info);
  filter->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_overlay_transform_frame_ip);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "qtioverlay", GST_RANK_PRIMARY,
          GST_TYPE_OVERLAY);
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    qtioverlay,
    "QTI Overlay. This plugin renders text, image, bounding box or graph on "
      "top of a video stream.",
    plugin_init,
    PACKAGE_VERSION,
    PACKAGE_LICENSE,
    PACKAGE_SUMMARY,
    PACKAGE_ORIGIN
)

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

#include "ml_meta.h"

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static gsize category = 0;

  if (g_once_init_enter (&category)) {
    gsize cat_done;

    cat_done = (gsize) _gst_debug_category_new("gstmlmeta", 0, "gstmlmeta");

    g_once_init_leave (&category, cat_done);
  }

  return (GstDebugCategory *) category;
}
#else
#define ensure_debug_category() /* NOOP */
#endif /* GST_DISABLE_GST_DEBUG */

static gboolean
gst_ml_detection_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMLDetectionMeta *bb_meta = (GstMLDetectionMeta *) meta;
  bb_meta->box_info = NULL;
  memset(&bb_meta->bounding_box, 0, sizeof(bb_meta->bounding_box));
  return TRUE;
}

static void
gst_ml_detection_free (GstMeta *meta, GstBuffer *buffer)
{
  GstMLDetectionMeta *bb_meta = (GstMLDetectionMeta *) meta;
  g_slist_free_full(bb_meta->box_info, free);
  GST_DEBUG ("free detection meta ts: %llu ", buffer->pts);
}

GType
gst_ml_detection_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstMLDetectionMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_ml_detection_get_info (void)
{
  static const GstMetaInfo *ml_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & ml_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_ML_DETECTION_API_TYPE,
            "GstMLDetectionMeta", (gsize) sizeof (GstMLDetectionMeta),
            (GstMetaInitFunction) gst_ml_detection_init,
            (GstMetaFreeFunction) gst_ml_detection_free,
            (GstMetaTransformFunction) NULL);
    g_once_init_leave ((GstMetaInfo **) & ml_meta_info, (GstMetaInfo *) meta);
  }
  return ml_meta_info;
}

static gboolean
gst_ml_segmentation_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMLSegmentationMeta *img_meta = (GstMLSegmentationMeta *) meta;
  img_meta->img_buffer = NULL;
  img_meta->img_width = 0;
  img_meta->img_height = 0;
  img_meta->img_size = 0;
  img_meta->img_format = GST_VIDEO_FORMAT_UNKNOWN;
  img_meta->img_stride = 0;
  return TRUE;
}

static void
gst_ml_segmentation_free (GstMeta *meta, GstBuffer *buffer)
{
  GstMLSegmentationMeta *img_meta = (GstMLSegmentationMeta *) meta;
  if (img_meta->img_buffer) {
    free(img_meta->img_buffer);
    img_meta->img_buffer = NULL;
  }
  GST_DEBUG ("free segmentation meta ts: %llu ", buffer->pts);
}

GType
gst_ml_segmentation_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstMLSegmentationMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_ml_segmentation_get_info (void)
{
  static const GstMetaInfo *ml_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & ml_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_ML_SEGMENTATION_API_TYPE,
            "GstMLSegmentationMeta", (gsize) sizeof (GstMLSegmentationMeta),
            (GstMetaInitFunction) gst_ml_segmentation_init,
            (GstMetaFreeFunction) gst_ml_segmentation_free,
            (GstMetaTransformFunction) NULL);
    g_once_init_leave ((GstMetaInfo **) & ml_meta_info, (GstMetaInfo *) meta);
  }
  return ml_meta_info;
}

static gboolean
gst_ml_classification_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMLClassificationMeta *l_meta = (GstMLClassificationMeta *) meta;
  l_meta->result.name = NULL;
  l_meta->result.confidence = 0.0;
  return TRUE;
}

static void
gst_ml_classification_free (GstMeta *meta, GstBuffer *buffer)
{
  GstMLClassificationMeta *l_meta = (GstMLClassificationMeta *) meta;
  if (l_meta->result.name) {
    free(l_meta->result.name);
    l_meta->result.name = NULL;
  }
  GST_DEBUG ("free classification meta ts: %llu ", buffer->pts);
}

GType
gst_ml_classification_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstMLClassificationMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_ml_classification_get_info (void)
{
  static const GstMetaInfo *ml_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & ml_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_ML_CLASSIFICATION_API_TYPE,
            "GstMLClassificationMeta", (gsize) sizeof (GstMLClassificationMeta),
            (GstMetaInitFunction) gst_ml_classification_init,
            (GstMetaFreeFunction) gst_ml_classification_free,
            (GstMetaTransformFunction) NULL);
    g_once_init_leave ((GstMetaInfo **) & ml_meta_info, (GstMetaInfo *) meta);
  }
  return ml_meta_info;
}

static gboolean
gst_ml_posenet_init (GstMeta * meta, gpointer params, GstBuffer * buffer)
{
  GstMLPoseNetMeta *p_meta = (GstMLPoseNetMeta *) meta;
  memset(p_meta->points, 0, sizeof(p_meta->points));
  p_meta->score = 0.0;
  return TRUE;
}

GType
gst_ml_posenet_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstMLPoseNetMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_ml_posenet_get_info (void)
{
  static const GstMetaInfo *ml_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & ml_meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_ML_POSENET_API_TYPE,
            "GstMLPoseNetMeta", (gsize) sizeof (GstMLPoseNetMeta),
            (GstMetaInitFunction) gst_ml_posenet_init,
            (GstMetaFreeFunction) NULL,
            (GstMetaTransformFunction) NULL);
    g_once_init_leave ((GstMetaInfo **) & ml_meta_info, (GstMetaInfo *) meta);
  }
  return ml_meta_info;
}

GstMLDetectionMeta *
gst_buffer_add_detection_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  GstMLDetectionMeta *meta =
      (GstMLDetectionMeta *) gst_buffer_add_meta (buffer,
          GST_ML_DETECTION_INFO, NULL);

  return meta;
}

GSList *
gst_buffer_get_detection_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  const GstMetaInfo *info = GST_ML_DETECTION_INFO;

  g_return_val_if_fail (buffer != NULL, NULL);

  GSList *meta_list = NULL;
  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      meta_list = g_slist_prepend(meta_list, meta);
    }
  }
  return meta_list;
}

GstMLSegmentationMeta *
gst_buffer_add_segmentation_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  GstMLSegmentationMeta *meta =
      (GstMLSegmentationMeta *) gst_buffer_add_meta (buffer,
        GST_ML_SEGMENTATION_INFO, NULL);

  return meta;
}

GSList *
gst_buffer_get_segmentation_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  const GstMetaInfo *info = GST_ML_SEGMENTATION_INFO;

  g_return_val_if_fail (buffer != NULL, NULL);

  GSList *meta_list = NULL;
  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      meta_list = g_slist_prepend(meta_list, meta);
    }
  }
  return meta_list;
}

GstMLClassificationMeta *
gst_buffer_add_classification_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  GstMLClassificationMeta *meta =
      (GstMLClassificationMeta *)
          gst_buffer_add_meta (buffer, GST_ML_CLASSIFICATION_INFO, NULL);

  return meta;
}

GSList *
gst_buffer_get_classification_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  const GstMetaInfo *info = GST_ML_CLASSIFICATION_INFO;

  g_return_val_if_fail (buffer != NULL, NULL);

  GSList *meta_list = NULL;
  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      meta_list = g_slist_prepend(meta_list, meta);
    }
  }
  return meta_list;
}

GstMLPoseNetMeta *
gst_buffer_add_posenet_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (buffer != NULL, NULL);

  GstMLPoseNetMeta *meta =
      (GstMLPoseNetMeta *)
          gst_buffer_add_meta (buffer, GST_ML_POSENET_INFO, NULL);

  return meta;
}

GSList *
gst_buffer_get_posenet_meta (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta = NULL;
  const GstMetaInfo *info = GST_ML_POSENET_INFO;

  g_return_val_if_fail (buffer != NULL, NULL);

  GSList *meta_list = NULL;
  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    if (meta->info->api == info->api) {
      meta_list = g_slist_prepend(meta_list, meta);
    }
  }
  return meta_list;
}

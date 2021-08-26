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

#ifndef __GST_ML_META_H__
#define __GST_ML_META_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _GstMLClassificationResult GstMLClassificationResult;
typedef struct _GstMLBoundingBox GstMLBoundingBox;
typedef struct _GstMLDetectionMeta GstMLDetectionMeta;
typedef struct _GstMLSegmentationMeta GstMLSegmentationMeta;
typedef struct _GstMLClassificationMeta GstMLClassificationMeta;

typedef struct _GstMLKeyPoint GstMLKeyPoint;
typedef struct _GstMLPose GstMLPose;
typedef struct _GstMLPoseNetMeta GstMLPoseNetMeta;

#define GST_ML_DETECTION_API_TYPE (gst_ml_detection_get_type())
#define GST_ML_DETECTION_INFO (gst_ml_detection_get_info())

#define GST_ML_SEGMENTATION_API_TYPE (gst_ml_segmentation_get_type())
#define GST_ML_SEGMENTATION_INFO (gst_ml_segmentation_get_info())

#define GST_ML_CLASSIFICATION_API_TYPE (gst_ml_classification_get_type())
#define GST_ML_CLASSIFICATION_INFO (gst_ml_classification_get_info())

#define GST_ML_POSENET_API_TYPE (gst_ml_posenet_get_type())
#define GST_ML_POSENET_INFO (gst_ml_posenet_get_info())


/**
 * GstMLBoundingBox:
 * @x: horizontal start position
 * @y: vertical start position
 * @width: active window width
 * @height: active window height

 * Bounding box properties
 */
struct _GstMLBoundingBox {
  guint x;
  guint y;
  guint width;
  guint height;
};

/**
 * GstMLClassificationResult:
 * @name: name for given object
 * @confidence: confidence for given object
 *
 * Name and confidence handle
 */
struct _GstMLClassificationResult {
  gchar  *name;
  gfloat confidence;
};

/**
 * GstMLDetectionMeta:
 * @parent: parent #GstMeta
 * @box: bounding box coordinates
 * @box_info: list of GstMLClassificationResult which handle names and confidences
 *
 * Machine learning SSD models properties
 */
struct _GstMLDetectionMeta {
  GstMeta           parent;
  GstMLBoundingBox  bounding_box;
  GSList            *box_info;
  guint             bbox_color;
};

/**
 * GstMLSegmentationMeta:
 * @parent: parent #GstMeta
 * @img_buffer: pointer to segmentation image data
 * @img_width: the segmentation image width in pixels
 * @img_height: the segmentation image height in pixels
 * @img_size: size of image buffer in bytes
 * @img_format: the segmentation image pixel format
 * @img_stride: the segmentation image bytes per line
 *
 * Machine learning segmentation image models properties
 */
struct _GstMLSegmentationMeta {
  GstMeta         parent;
  gpointer        img_buffer;
  guint           img_width;
  guint           img_height;
  guint           img_size;
  GstVideoFormat  img_format;
  guint           img_stride;
};

/**
 * GstMLClassificationMeta:
 * @parent: parent #GstMeta
 * @result:  name and confidence
 * @location: location in frame of location is CUSTOM then x/y are considered
 * @x: horizontal start position if location is CUSTOM
 * @y: vertical start position if location is CUSTOM
 *
 * Machine learning classification models properties
 */
struct _GstMLClassificationMeta {
  GstMeta                   parent;
  GstMLClassificationResult result;
};

/**
 * GstMLKeyPoints - PoseNet key points
 */
enum GstMLKeyPointsType{
  NOSE,
  LEFT_EYE,
  RIGHT_EYE,
  LEFT_EAR,
  RIGHT_EAR,
  LEFT_SHOULDER,
  RIGHT_SHOULDER,
  LEFT_ELBOW,
  RIGHT_ELBOW,
  LEFT_WRIST,
  RIGHT_WRIST,
  LEFT_HIP,
  RIGHT_HIP,
  LEFT_KNEE,
  RIGHT_KNEE,
  LEFT_ANKLE,
  RIGHT_ANKLE,
  KEY_POINTS_COUNT
};

/**
 * GstMLKeyPoint:
 * @x: x coordinate
 * @y: y coordinate
 * @score: score of given pose
 *
 * Machine learning PoseNet poses
 */
struct _GstMLKeyPoint {
  gint              x;
  gint              y;
  gfloat            score;
};

/**
 * GstMLPoseNetMeta:
 * @parent: parent #GstMeta
 * @points: array of key points coordinates and score.
 *          Key points order corresponds to GstMLKeyPointsType.
 * @score: score of all poses
 *
 * Machine learning PoseNet models properties
 */
struct _GstMLPoseNetMeta {
  GstMeta           parent;
  GstMLKeyPoint     points[KEY_POINTS_COUNT];
  gfloat            score;
};


GType gst_ml_detection_get_type (void);
const GstMetaInfo * gst_ml_detection_get_info (void);
GType gst_ml_segmentation_get_type (void);
const GstMetaInfo * gst_ml_segmentation_get_info (void);
GType gst_ml_classification_get_type (void);
const GstMetaInfo * gst_ml_classification_get_info (void);
GType gst_ml_posenet_get_type (void);
const GstMetaInfo * gst_ml_posenet_get_info (void);

/**
 * gst_buffer_add_detection_meta:
 * @buffer: the buffer new metadata belongs to
 *
 * Creates new bounding detection entry and returns pointer to new
 * entry. Metadata payload is not input parameter in order to avoid
 * unnecessary copy of data.
 *
 */
GST_EXPORT
GstMLDetectionMeta * gst_buffer_add_detection_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_detection_meta:
 * @buffer: the buffer metadata comes from
 *
 * Returns list of bounding detection entries. List payload should be
 * considered as GstMLDetectionMeta. Caller is supposed to free the list.
 *
 */
GST_EXPORT
GSList * gst_buffer_get_detection_meta (GstBuffer * buffer);

/**
 * gst_buffer_add_segmentation_meta:
 * @buffer: the buffer new metadata belongs to
 *
 * Creates new segmentation metadata entry and returns pointer to new
 * entry. Metadata payload is not input parameter in order to avoid
 * unnecessary copy of data.
 *
 */
GST_EXPORT
GstMLSegmentationMeta * gst_buffer_add_segmentation_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_segmentation_meta:
 * @buffer: the buffer metadata comes from
 *
 * Returns list of segmentation metadata entries. List payload should be
 * considered as GstMLSegmentationMeta. Caller is supposed to free the list.
 *
 */
GST_EXPORT
GSList * gst_buffer_get_segmentation_meta (GstBuffer * buffer);

/**
 * gst_buffer_add_classification_meta:
 * @buffer: the buffer new metadata belongs to
 *
 * Creates new classification metadata entry and returns pointer to new
 * entry. Metadata payload is not input parameter in order to avoid
 * unnecessary copy of data.
 *
 */
GST_EXPORT
GstMLClassificationMeta * gst_buffer_add_classification_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_classification_meta:
 * @buffer: the buffer metadata comes from
 *
 * Returns list of classification metadata entries. List payload should be
 * considered as GstMLClassificationMeta. Caller is supposed to free the list.
 *
 */
GST_EXPORT
GSList * gst_buffer_get_classification_meta (GstBuffer * buffer);

/**
 * gst_buffer_add_posenet_meta:
 * @buffer: the buffer new metadata belongs to
 *
 * Creates new posenet metadata entry and returns pointer to new
 * entry. Metadata payload is not input parameter in order to avoid
 * unnecessary copy of data.
 *
 */
GST_EXPORT
GstMLPoseNetMeta * gst_buffer_add_posenet_meta (GstBuffer * buffer);

/**
 * gst_buffer_get_posenet_meta:
 * @buffer: the buffer metadata comes from
 *
 * Returns list of posenet metadata entries. List payload should be
 * considered as GstMLPoseNetMeta. Caller is supposed to free the list.
 *
 */
GST_EXPORT
GSList * gst_buffer_get_posenet_meta (GstBuffer * buffer);

G_END_DECLS

#endif /* __GST_ML_META_H__ */

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

#define KERNEL_X_SIZE 1.0f
#define KERNEL_Y_SIZE 1.0f

const sampler_t smp =
    CLK_NORMALIZED_COORDS_TRUE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;

kernel void overlay_cl(__read_only image2d_t mask, // 1
                       __global uchar *frame,      // 2
                       uint y_offset,              // 3
                       uint nv_offset,             // 4
                       ushort stride,              // 5
                       ushort swap_uv              // 6
) {
  uint x = get_global_id(0);
  uint y = get_global_id(1);

  // Read input yuv data
  uint offset = 2 * (stride * y + x);
  uchar2 y_out1 = *(frame + y_offset + offset);
  uchar2 y_out2 = *(frame + y_offset + offset + stride);

  offset = stride * y + x * 2;
  uchar2 uv_out = *(__global uchar2 *)(frame + nv_offset + offset);

  // Read and resize mask data
  float2 coord;
  coord.s0 = (KERNEL_X_SIZE * x) / get_global_size(0);
  coord.s1 = (KERNEL_Y_SIZE * y) / get_global_size(1);
  uchar4 mask_data = convert_uchar4(read_imageui(mask, smp, coord));

  // Convert rgb to yuv
  float luma;
  float2 chroma;

  luma =
      0.2126f * mask_data.s0 + 0.7152f * mask_data.s1 + 0.0722f * mask_data.s2;
  chroma.s0 = -0.09991f * mask_data.s0 - 0.33609f * mask_data.s1 +
              0.436f * mask_data.s2;
  chroma.s1 =
      0.615f * mask_data.s0 - 0.55861 * mask_data.s1 - 0.05639f * mask_data.s2;
  chroma += 128;

  if (swap_uv) {
    chroma.s01 = chroma.s10;
  }

  luma = clamp(luma, 0.0f, 255.0f);
  chroma = clamp(chroma, 0.0f, 255.0f);

  // Apply alpha blending
  float alpha = mask_data.s3 / 255.0f;
  y_out1 =
      convert_uchar2(alpha * luma + (1.0f - alpha) * convert_float2(y_out1));
  y_out2 =
      convert_uchar2(alpha * luma + (1.0f - alpha) * convert_float2(y_out2));
  uv_out =
      convert_uchar2(alpha * chroma + (1.0f - alpha) * convert_float2(uv_out));

  // Store output yuv data
  offset = 2 * (stride * y + x);
  *(__global uchar2 *)(frame + y_offset + offset) = y_out1;
  *(__global uchar2 *)(frame + y_offset + offset + stride) = y_out2;

  offset = stride * y + x * 2;
  *(__global uchar2 *)(frame + nv_offset + offset) = uv_out;
}

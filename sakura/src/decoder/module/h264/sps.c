// sps.c

#include "std/fileio.h"
#include "std/stdio.h"
#include "std/common.h"
#include "util/mem.h"

#include "decoder/h264/bitstream.h"
#include "decoder/h264/nalu.h"
#include "decoder/h264/rbsp.h"
#include "decoder/h264/sps.h"

static const uint8_t Default_4x4_Intra[16] = {
    6, 13, 13, 20, 20, 20, 28, 28, 28, 28, 32, 32, 32, 37, 37, 42
};


static const uint8_t Default_4x4_Inter[16] = {
    10, 14, 14, 20, 20, 20, 24, 24, 24, 24, 27, 27, 27, 30, 30, 34
};


static const uint8_t Default_8x8_Intra[64] = {
    6, 10, 10, 13, 11, 13, 16, 16, 16, 16, 18, 18, 18, 18, 18, 23,
    23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
    27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
    31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};


static const uint8_t Default_8x8_Inter[64] = {
    9, 13, 13, 15, 13, 15, 17, 17, 17, 17, 19, 19, 19, 19, 19, 21,
    23, 23, 23, 23, 23, 25, 25, 25, 25, 25, 25, 25, 27, 27, 27, 27,
    27, 27, 27, 27, 29, 29, 29, 29, 29, 29, 29, 31, 31, 31, 31, 31,
    31, 33, 33, 33, 33, 33, 36, 36, 36, 36, 38, 38, 38, 40, 40, 42
};

void free_sps(SPS_t *sps) {

}

void scaling_list(SPS_t *sps, Bitstream *bs, uint8_t *scalingList, int sizeOfScalingList, uint8_t useDefaultScalingMatrixFlag) {
    int lastScale = 8;
    int nextScale = 8;
    for (int j = 0; j < sizeOfScalingList; j++) {
        if (nextScale != 0) {
            sps->delta_scale = read_se(bs);
            nextScale = (lastScale + sps->delta_scale + 256) % 256;
            useDefaultScalingMatrixFlag = (j == 0 && nextScale == 0);
        }
        scalingList[j] = (nextScale == 0) ? lastScale : nextScale;
        lastScale = scalingList[j];
    }
}

void hrd_parameters(SPS_t *sps, Bitstream *bs) {
  sps->cpb_cnt_minus1 = read_ue(bs);
  sps->bit_rate_scale = read_bit(4);
  sps->cpb_size_scale = read_bit(4);
  uint32_t cpb_cnt = sps->cpb_cnt_minus1 + 1;
  sps->bit_rate_value_minus1 = (uint32_t *)mmalloc(sizeof(uint32_t) * cpb_cnt);
  sps->cpb_size_value_minus1 = (uint32_t *)mmalloc(sizeof(uint32_t) * cpb_cnt);
  sps->cbr_flag = (uint8_t *)mmalloc(sizeof(uint8_t) * cpb_cnt);
  for (int SchedSelIdx = 0; SchedSelIdx < cpb_cnt; SchedSelIdx++) {
    sps->bit_rate_value_minus1[SchedSelIdx] = read_ue(bs);
    sps->cpb_size_value_minus1[SchedSelIdx] = read_ue(bs);
    sps->cbr_flag[SchedSelIdx] = read_bit(bs);
  }
  sps->initial_cpb_removal_delay_length_minus1 = read_bits(bs, 5);
  sps->cpb_removal_delay_length_minus1 = read_bits(bs, 5);
  sps->dpb_output_delay_length_minus1 = read_bits(bs, 5);
  sps->time_offset_length = read_bits(bs, 5);
}

SPS_t* rbsp_to_sps(RBSP_t *rbsp) {
  SPS_t *sps = mmalloc(sizeof(SPS_t));
  sps->buffer = (uint8_t *)mmalloc(rbsp->rbsp_size);
  if (!sps->buffer) {
      free_sps(sps);
      return NULL;
  }

  Bitstream* bs = mmalloc(sizeof(Bitstream));
  bs = init_bs(bs, rbsp->buffer + 1, rbsp->rbsp_size - 1);

  sps->profile_idc = read_bits(bs, 8);
  sps->constraint_set_flags = (read_bits(bs, 8 ) >> 2);
  sps->level_idc = read_bits(bs, 8);
  sps->seq_parameter_set_id = read_ue(bs);
  if (sps->profile_idc == 100 || sps->profile_idc == 110
      || sps->profile_idc == 122 || sps->profile_idc == 244
      || sps->profile_idc == 44 || sps->profile_idc == 83
      || sps->profile_idc == 86 || sps->profile_idc == 118
      || sps->profile_idc == 128 || sps->profile_idc == 138
      || sps->profile_idc == 139 || sps->profile_idc == 134
      || sps->profile_idc == 135) {
      sps->chroma_format_idc = read_ue(bs);
      if (sps->chroma_format_idc == 3) {
          sps->separate_color_plane_flag = read_bit(bs);
      }
      sps->bit_depth_luma_minus8 = read_ue(bs);
      sps->bit_depth_chroma_minus8 = read_ue(bs);
      sps->qpprime_y_zero_transform_bypass_flag = read_bit(bs);
      sps->seq_scaling_matrix_present_flag = read_bit(bs);
      if (sps->seq_scaling_matrix_present_flag) {
          for (int i = 0; i < 8; i++) {
              sps->seq_scaling_list_present_flag[i] = read_bit(bs);
              // if (sps->seq_scaling_list_present_flag[i]) {
              //     if (i == 0 || i == 1 || i == 2) {
              //         scaling_list(sps, bs, Default_4x4_Intra[i], 16, UseDefaultScalingMatrix4x4Flag[i]);
              //     } else {
              //         scaling_list(sps, bs, ScalingList8x8[i - 6], 64, UseDefaultScalingMatrix8x8Flag[i - 6]);
              //     }
              // }
          }
      }
  }
  sps->log2_max_frame_num_minus4 = read_ue(bs);
  sps->pic_order_cnt_type = read_ue(bs);
  if (sps->pic_order_cnt_type == 0) {
      sps->log2_max_pic_order_cnt_lsb_minus4 = read_ue(bs);
  } else if (sps->pic_order_cnt_type == 1) {
      sps->delta_pic_order_always_zero_flag = read_bit(bs);
      sps->offset_for_non_red_pic = read_se(bs);
      sps->offset_for_top_to_bottom_field = read_se(bs);
      sps->num_ref_frames_in_pic_order_cnt_cycle = read_ue(bs);
      sps->offset_for_ref_frame = (uint32_t *)mmalloc(sizeof(uint32_t) * sps->num_ref_frames_in_pic_order_cnt_cycle);
      for (int i = 0; i < sps->num_ref_frames_in_pic_order_cnt_cycle; i++) {
          sps->offset_for_ref_frame[i] = read_se(bs);
      }
  }
  sps->max_num_ref_frames = read_ue(bs);
  sps->gaps_in_frame_num_value_allowed_flag = read_bit(bs);
  sps->pic_width_in_mbs_minus1 = read_ue(bs);
  sps->pic_height_in_map_units_minus1 = read_ue(bs);
  sps->frame_mbs_only_flag = read_bit(bs);
  if (!sps->frame_mbs_only_flag) {
      sps->mb_adaptive_frame_field_flag = read_bit(bs);
  }
  sps->direct_8x8_inference_flag = read_bit(bs);
  sps->frame_cropping_flag = read_bit(bs);
  if (sps->frame_cropping_flag) {
      sps->frame_crop_left_offset = read_ue(bs);
      sps->frame_crop_bottom_offset = read_ue(bs);
      sps->frame_crop_top_offset = read_ue(bs);
      sps->frame_crop_bottom_offset = read_ue(bs);
  }
  sps->vui_parameters_present_flag = read_bit(bs);

  if (sps->vui_parameters_present_flag) {
      sps->aspect_ratio_info_present_flag = read_bit(bs);
      if (sps->aspect_ratio_info_present_flag) {
          sps->aspect_ratio_idc = read_bits(bs, 8);
          if (sps->aspect_ratio_idc == 255) {
              sps->sar_width = read_bits(bs, 16);
              sps->sar_height = read_bits(bs, 16);
          }
      }
      sps->overscan_info_present_flag = read_bit(bs);
      if (sps->overscan_info_present_flag) {
          sps->overscan_appropriate_flag = read_bit(bs);
      }
      sps->video_signal_type_present_flag = read_bit(bs);
      if (sps->video_signal_type_present_flag) {
          sps->video_format = read_bits(bs, 3);
          sps->video_full_range_flag = read_bit(bs);
          sps->color_description_present_flag = read_bit(bs);
          if (sps->color_description_present_flag) {
              sps->color_primaries = read_bits(bs, 8);
              sps->transfer_characteristics = read_bits(bs, 8);
              sps->matrix_coefficients = read_bits(bs, 8);
          }
      }
      sps->chroma_loc_info_present_flag = read_bit(bs);
      if (sps->chroma_loc_info_present_flag) {
          sps->chroma_sample_loc_type_top_field = read_ue(bs);
          sps->chroma_sample_loc_type_bottom_field = read_ue(bs);
      }
      sps->timing_info_present_flag = read_bit(bs);
      if (sps->timing_info_present_flag) {
          sps->num_units_in_tick = read_bits(bs, 32);
          sps->time_scale= read_bits(bs, 32);
          sps->fixed_frame_rate_flag = read_bit(bs);
      }
      sps->nal_hrd_parameters_present_flag = read_bit(bs);
      if (sps->nal_hrd_parameters_present_flag) {
        hrd_parameters(sps, bs);
      }
      sps->vcl_hrd_parameters_present_flag = read_bit(bs);
      if (sps->vcl_hrd_parameters_present_flag) {
        hrd_parameters(sps, bs);
      }
      if (sps->nal_hrd_parameters_present_flag || sps->vcl_hrd_parameters_present_flag) {
          sps->low_delay_hrd_flag = read_bit(bs);
      }
      sps->pic_struct_present_flag = read_bit(bs);
      sps->bitstream_restriction_flag = read_bit(bs);
      if (sps->bitstream_restriction_flag) {
          sps->motion_vectors_over_pic_boundaries_flag = read_bit(bs);
          sps->max_bytes_per_pic_denom = read_ue(bs);
          sps->max_bits_per_mb_denom = read_ue(bs);
          sps->log2_max_mv_length_horizontal = read_ue(bs);
          sps->log2_max_mv_length_vertical = read_ue(bs);
          sps->max_num_reorder_frames = read_ue(bs);
          sps->max_dec_frame_buffering = read_ue(bs);
      }
  }
  mfree(sps->buffer);
}


// int main() {
//   const char *filename = "annexb.h264";
//   FILE *file = open_file(filename);
//   if (!file) {
//     pprintf("Error opening the file");
//     return;
//   }
//
//   int nalu_count = 0;
//
//   while (1) {
//     NALU_t *nalu = read_annexb_nalu(file);
//     if (!nalu) break;
//
//     nalu_count++;
//     pprintf("NALU #%d:\n", nalu_count);
//     // print_nalu(nalu);
//     pprintf("NALU #%d size: %d bytes\n\n", nalu_count, nalu->nalu_size);
//
//     RBSP_t *rbsp = nalu_to_rbsp(nalu);
//     if (rbsp->nal_unit_type == 7) {
//         SPS_t *sps = mmalloc(sizeof(sps));
//         sps = rbsp_to_sps(rbsp);
//     }
//
//     print_rbsp(rbsp);
//
//     free_nalu(nalu);
//   }
//   close_file(file);
// }

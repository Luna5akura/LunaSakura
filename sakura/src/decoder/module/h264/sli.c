// sli.c

#include "std/fileio.h"
#include "std/stdio.h"
#include "util/mem.h"
#include "std/math.h"
#include "decoder/h264/nalu.h"
#include "decoder/h264/rbsp.h"
#include "decoder/h264/sps.h"
#include "decoder/h264/pps.h"
#include "decoder/h264/bitstream.h"

#include "decoder/h264/sli.h"

void free_sli(SLI_t *sli) {

}

void ref_pic_list_mvc_modification(SLI_t *sli, Bitstream *bs) {
  if (sli->slice_type % 5 != 2 && sli->slice_type % 5 != 4) {
    sli->ref_pic_list_modification_flag_l0 = read_bit(bs);
    if (sli->ref_pic_list_modification_flag_l0) {
      do {
        sli->modification_of_pic_nums_idc = read_ue(bs);
        if (sli->modification_of_pic_nums_idc == 0 || sli->modification_of_pic_nums_idc == 1) {
          sli->abs_diff_pic_num_minus1 = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 2) {
          sli->long_term_pic_num = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 4 || sli->modification_of_pic_nums_idc == 5) {
          sli->abs_diff_view_idx_minus1 = read_ue(bs);
        }
      } while (sli->modification_of_pic_nums_idc != 3);
    }
  }

  if (sli->slice_type % 5 == 1) {
    sli->ref_pic_list_modification_flag_l0 = read_bit(bs);
    if (sli->ref_pic_list_modification_flag_l0) {
      do {
        sli->modification_of_pic_nums_idc = read_ue(bs);
        if (sli->modification_of_pic_nums_idc == 0 || sli->modification_of_pic_nums_idc == 1) {
          sli->abs_diff_pic_num_minus1 = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 2) {
          sli->long_term_pic_num = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 4 || sli->modification_of_pic_nums_idc == 5) {
          sli->abs_diff_view_idx_minus1 = read_ue(bs);
        }
      } while (sli->modification_of_pic_nums_idc != 3);
    }
  }
}

void ref_pic_list_modification(SLI_t *sli, Bitstream *bs) {
  if (sli->slice_type % 5 != 2 && sli->slice_type % 5 != 4) {
    sli->ref_pic_list_modification_flag_l0 = read_bit(bs);
    if (sli->ref_pic_list_modification_flag_l0) {
      do {
        sli->modification_of_pic_nums_idc = read_ue(bs);
        if (sli->modification_of_pic_nums_idc == 0 || sli->modification_of_pic_nums_idc == 1) {
          sli->abs_diff_pic_num_minus1 = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 2) {
          sli->long_term_pic_num = read_ue(bs);
        }
      } while (sli->modification_of_pic_nums_idc != 3);
    }
  }

  if (sli->slice_type % 5 == 1) {
    sli->ref_pic_list_modification_flag_l0 = read_bit(bs);
    if (sli->ref_pic_list_modification_flag_l0) {
      do {
        sli->modification_of_pic_nums_idc = read_ue(bs);
        if (sli->modification_of_pic_nums_idc == 0 || sli->modification_of_pic_nums_idc == 1) {
          sli->abs_diff_pic_num_minus1 = read_ue(bs);
        } else if (sli->modification_of_pic_nums_idc == 2) {
          sli->long_term_pic_num = read_ue(bs);
        }
      } while (sli->modification_of_pic_nums_idc != 3);
    }
  }
}

void pred_weight_table(SLI_t *sli, SPS_t *sps, Bitstream *bs) {
  sli->luma_log2_weight_denom = read_ue(bs);
  uint32_t ChromaArrayType = (sps->separate_color_plane_flag == 0) ? sps->chroma_format_idc : 0;
  if (ChromaArrayType != 0) {
    sli->chroma_log2_weight_denom = read_ue(bs);
  }
  sli->luma_weight_l0 = (int32_t *)mmalloc(sizeof(int32_t) * sli->num_ref_idx_l0_active_minus1 + 1);
  sli->luma_offset_l0 = (int32_t *)mmalloc(sizeof(int32_t) * sli->num_ref_idx_l0_active_minus1 + 1);
  sli->chroma_weight_l0 = (int32_t **)mmalloc(sizeof(int32_t *) * sli->num_ref_idx_l0_active_minus1 + 1);
  sli->chroma_offset_l0 = (int32_t **)mmalloc(sizeof(int32_t *) * sli->num_ref_idx_l0_active_minus1 + 1);

  for (uint32_t i = 0; i < sli->num_ref_idx_l0_active_minus1 + 1; i++) {
    sli->luma_weight_l0_flag = read_bit(bs);
    if (sli->luma_weight_l0_flag) {
      sli->luma_weight_l0[i] = read_se(bs);
      sli->luma_offset_l0[i] = read_se(bs);
    }
    if (ChromaArrayType != 0) {
      sli->chroma_weight_l0_flag = read_bit(bs);
      if (sli->chroma_weight_l0_flag) {
        sli->chroma_weight_l0[i] = (int32_t *)mmalloc(sizeof(int32_t) * 2);
        sli->chroma_offset_l0[i] = (int32_t *)mmalloc(sizeof(int32_t) * 2);
        for (int j = 0; j < 2; j++) {
          sli->chroma_weight_l0[i][j] = read_se(bs);
          sli->chroma_offset_l0[i][j] = read_se(bs);
        }
      }
    }
  }
  if (sli->slice_type % 5 == 1) {
    for (uint32_t i = 0; i < sli->num_ref_idx_l1_active_minus1 + 1; i++) {
      sli->luma_weight_l1_flag = read_bit(bs);
      if (sli->luma_weight_l1_flag) {
        sli->luma_weight_l1[i] = read_se(bs);
        sli->luma_offset_l1[i] = read_se(bs);
      }
      if (ChromaArrayType != 0) {
        sli->chroma_weight_l1_flag = read_bit(bs);
        if (sli->chroma_weight_l1_flag) {
          sli->chroma_weight_l1[i] = (int32_t *)mmalloc(sizeof(int32_t) * 2);
          sli->chroma_offset_l1[i] = (int32_t *)mmalloc(sizeof(int32_t) * 2);
          for (int j = 0; j < 2; j++) {
            sli->chroma_weight_l1[i][j] = read_se(bs);
            sli->chroma_offset_l1[i][j] = read_se(bs);
          }
        }
      }
    }
  }
}

void dec_ref_pic_marking(SLI_t *sli, RBSP_t *rbsp, Bitstream *bs) {
  int IdrPicFlag = ((rbsp->nal_unit_type == 5) ? 1 : 0);
  if (IdrPicFlag) {
    sli->no_output_of_prior_pics_flag = read_bit(bs);
    sli->long_term_reference_flag = read_bit(bs);
  } else {
    sli->adaptive_ref_pic_marking_mode_flag = read_bit(bs);
    if (sli->adaptive_ref_pic_marking_mode_flag) {
      do {
        sli->memory_management_control_operation = read_ue(bs);
        if (sli->memory_management_control_operation == 1 || sli->memory_management_control_operation == 3) {
          sli->difference_of_pic_nums_minus1 = read_ue(bs);
        }
        if (sli->memory_management_control_operation == 2) {
          sli->long_term_pic_num = read_ue(bs);
        }
        if (sli->memory_management_control_operation == 3 || sli->memory_management_control_operation == 6) {
          sli->long_term_frame_idx = read_ue(bs);
        }
        if (sli->memory_management_control_operation == 4) {
          sli->max_long_term_frame_idx_plus1 = read_ue(bs);
        }
      } while (sli->memory_management_control_operation != 0);
    }
  }
}

void slice_header(SLI_t *sli, SPS_t *sps, PPS_t *pps, RBSP_t *rbsp, Bitstream *bs) {
  sli->first_mb_in_slice = read_ue(bs);
  sli->slice_type = read_ue(bs);
  sli->pic_parameter_set_id = read_ue(bs);
  if (sps->separate_color_plane_flag == 1) {
    sli->color_plane_id = read_bits(bs, 2);
  }
  sli->frame_num = read_bits(bs, sps->log2_max_frame_num_minus4 + 4);
  if (!sps->frame_mbs_only_flag) {
    sli->field_pic_flag = read_bit(bs);
    if (sli->field_pic_flag) {
      sli->bottom_field_flag = read_bit(bs);
    }
  }
  int IdrPicFlag = ((rbsp->nal_unit_type == 5) ? 1 : 0);
  if (IdrPicFlag) {
    sli->idr_pic_id = read_ue(bs);
  }
  if (sps->pic_order_cnt_type == 0) {
    sli->pic_order_cnt_lsb = read_bits(bs, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (pps->bottom_field_pic_order_in_frame_present_flag && (!sli->field_pic_flag)) {
      sli->delta_pic_order_cnt_bottom = read_se(bs);
    }
  }
  if (sps->pic_order_cnt_type == 1 && (!sps->delta_pic_order_always_zero_flag)) {
    sli->delta_pic_order_cnt[0] = read_se(bs);
    if (pps->bottom_field_pic_order_in_frame_present_flag && (!sli->field_pic_flag)) {
      sli->delta_pic_order_cnt[1] = read_se(bs);
    }
  }
  if (pps->redundant_pic_cnt_present_flag) {
    sli->redundant_pic_cnt = read_ue(bs);
  }
  if (sli->slice_type % 5 == B) {
    sli->direct_spatial_mv_pred_flag = read_bit(bs);
  }
  if (sli->slice_type % 5 == P || sli->slice_type % 5 == SP || sli->slice_type % 5 == B) {
    sli->num_ref_idx_active_override_flag = read_bit(bs);
    if (sli->num_ref_idx_active_override_flag) {
      sli->num_ref_idx_l0_active_minus1 = read_ue(bs);
      if (sli->slice_type % 5 == B) {
        sli->num_ref_idx_l1_active_minus1 = read_ue(bs);
      }
    }
  }
  if (rbsp->nal_unit_type == 20 || rbsp->nal_unit_type == 21) {
    ref_pic_list_mvc_modification();
  } else {
    ref_pic_list_modification(sli, bs);
  }
  if ((pps->weighted_pred_flag && (sli->slice_type % 5 == P || sli->slice_type % 5 == SP)) || (pps->weighted_bipred_idc == 1 && sli->slice_type % 5 == B)) {
    pred_weight_table(sli, sps, bs);
  }
  if (rbsp->nal_ref_idc != 0) {
    dec_ref_pic_marking(sli, rbsp, bs);
  }
  if (pps->entropy_coding_mode_flag && sli->slice_type != 2 && sli->slice_type != 9) {
    sli->cabac_init_idc = read_ue(bs);
  }
  sli->slice_qp_delta = read_se(bs);
  if (sli->slice_type % 5 == SP || sli->slice_type % 5 == SI) {
    if (sli->slice_type % 5 == SP) {
      sli->sp_for_switch_flag = read_bit(bs);
    }
    sli->slice_qs_delta = read_se(bs);
  }
  if (pps->deblocking_filter_control_present_flag) {
    sli->disable_deblocking_filter_idc = read_ue(bs);
    if (sli->disable_deblocking_filter_idc != 1) {
      sli->slice_alpha_c0_offset_div2 = read_se(bs);
      sli->slice_beta_offset_div_2 = read_se(bs);
    }
  }
  if (pps->num_slice_groups_minus1 > 0 && pps->slice_group_map_type >= 3 && pps->slice_group_map_type <= 5) {
    uint32_t PicWidthInMbs = sps->pic_width_in_mbs_minus1 + 1;
    uint32_t PicHeightInMapUnits = sps->pic_height_in_map_units_minus1 + 1;
    uint32_t PicSizeInMapUnits = PicWidthInMbs * PicHeightInMapUnits;
    uint32_t SliceGroupChangeRate = pps->slice_group_change_rate_minus_1 + 1;
    sli->slice_group_change_cycle = read_bits(bs, ceil(log((PicSizeInMapUnits / SliceGroupChangeRate + 1), 2)));
  }
}

void slice_data(SLI_t *sli, Bitstream *bs) {

}

SLI_t *rbsp_to_sli(RBSP_t *rbsp, SPS_t *sps, PPS_t *pps, SLI_t *sli) {
  sli->buffer = (uint8_t *)mmalloc(rbsp->rbsp_size);
  if (!sli->buffer) {
    free_sli(sli);
    return NULL;
  }

  Bitstream *bs = mmalloc(sizeof(Bitstream));
  bs = init_bs(bs, rbsp->buffer + 1, rbsp->rbsp_size - 1);

  slice_header(sli, sps, pps, rbsp, bs);
  slice_data();

  return sli;
}
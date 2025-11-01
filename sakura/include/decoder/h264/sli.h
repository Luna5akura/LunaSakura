// sli.h

#ifndef SLI_H
#define SLI_H

#include "std/common.h"

typedef enum {
  P,
  B,
  I,
  SP,
  SI,
} SLICETYPE;

typedef struct {
  uint8_t *buffer;
  uint32_t first_mb_in_slice;
  uint32_t slice_type;
  uint32_t pic_parameter_set_id;
  uint8_t color_plane_id;
  uint32_t frame_num;
  uint8_t field_pic_flag;
  uint8_t bottom_field_flag;
  uint32_t idr_pic_id;
  uint32_t pic_order_cnt_lsb;
  int32_t delta_pic_order_cnt_bottom;
  int32_t delta_pic_order_cnt[2];
  uint32_t redundant_pic_cnt;
  uint8_t direct_spatial_mv_pred_flag;
  uint8_t num_ref_idx_active_override_flag;
  uint32_t num_ref_idx_l0_active_minus1;
  uint32_t num_ref_idx_l1_active_minus1;
  uint32_t cabac_init_idc;
  int32_t slice_qp_delta;
  uint8_t sp_for_switch_flag;
  int32_t slice_qs_delta;
  uint32_t disable_deblocking_filter_idc;
  int32_t slice_alpha_c0_offset_div2;
  int32_t slice_beta_offset_div_2;
  uint32_t slice_group_change_cycle;

  uint8_t ref_pic_list_modification_flag_l0;
  uint32_t modification_of_pic_nums_idc;
  uint32_t abs_diff_pic_num_minus1;
  uint32_t long_term_pic_num;
  uint32_t abs_diff_view_idx_minus1;
  uint8_t ref_pic_list_modification_flag_l1;

  uint32_t luma_log2_weight_denom;
  uint32_t chroma_log2_weight_denom;
  uint8_t luma_weight_l0_flag;
  int32_t *luma_weight_l0;
  int32_t *luma_offset_l0;
  uint8_t chroma_weight_l0_flag;
  int32_t **chroma_weight_l0;
  int32_t ** chroma_offset_l0;
  uint8_t luma_weight_l1_flag;
  int32_t *luma_weight_l1;
  int32_t *luma_offset_l1;
  uint8_t chroma_weight_l1_flag;
  int32_t **chroma_weight_l1;
  int32_t ** chroma_offset_l1;

  uint8_t no_output_of_prior_pics_flag;
  uint8_t long_term_reference_flag;
  uint8_t adaptive_ref_pic_marking_mode_flag;
  uint32_t memory_management_control_operation;
  uint32_t difference_of_pic_nums_minus1;
  uint32_t long_term_frame_idx;
  uint32_t max_long_term_frame_idx_plus1;

  uint32_t mb_skip_run;

} SLI_t;

#endif

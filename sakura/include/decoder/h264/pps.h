// pps.h

#ifndef PPS_H
#define PPS_H

#include "std/common.h"

typedef struct {
  uint8_t *buffer;
  uint32_t pic_parameter_set_id;
  uint32_t seq_parameter_set_id;
  uint8_t entropy_coding_mode_flag;
  uint8_t bottom_field_pic_order_in_frame_present_flag;
  uint32_t num_slice_groups_minus1;
  uint32_t slice_group_map_type;
  uint32_t *run_length_minus1;
  uint32_t *top_left;
  uint32_t *bottom_right;
  uint8_t slice_group_change_direction_flag;
  uint32_t slice_group_change_rate_minus_1;
  uint32_t pic_size_in_map_units_minus1;
  uint32_t *slice_group_id;
  uint32_t num_ref_idx_l0_default_active_minus1;
  uint32_t num_ref_idx_l1_default_active_minus1;
  uint8_t weighted_pred_flag;
  uint8_t weighted_bipred_idc;
  uint32_t pic_init_qp_minus26;
  uint32_t pic_init_qs_minus26;
  uint32_t chroma_qp_index_offset;
  uint8_t deblocking_filter_control_present_flag;
  uint8_t constrained_intra_pred_flag;
  uint8_t redundant_pic_cnt_present_flag;
  uint8_t transform_8x8_mode_flag;
  uint8_t pic_scaling_matrix_present_flag;
  uint8_t *pic_scaling_list_present_flag;
  uint32_t second_chroma_qp_index_offset;
} PPS_t;

PPS_t* rbsp_to_pps(RBSP_t *rbsp, SPS_t *sps);

#endif

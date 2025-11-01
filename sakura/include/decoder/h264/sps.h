// sps.h

#ifndef SPS_H
#define SPS_H

#include "std/common.h"

typedef struct {
    int sps_size;
    uint8_t *buffer;
    uint8_t profile_idc;
    uint8_t constraint_set_flags;
    uint8_t level_idc;
    uint32_t seq_parameter_set_id;
    uint32_t chroma_format_idc;
    uint8_t separate_color_plane_flag;
    uint32_t bit_depth_luma_minus8;
    uint32_t bit_depth_chroma_minus8;
    uint8_t qpprime_y_zero_transform_bypass_flag;
    uint8_t seq_scaling_matrix_present_flag;
    uint8_t seq_scaling_list_present_flag[8];
    uint32_t log2_max_frame_num_minus4;
    uint32_t pic_order_cnt_type;
    uint32_t log2_max_pic_order_cnt_lsb_minus4;
    uint8_t delta_pic_order_always_zero_flag;
    uint32_t offset_for_non_red_pic;
    uint32_t offset_for_top_to_bottom_field;
    uint32_t num_ref_frames_in_pic_order_cnt_cycle;
    uint32_t *offset_for_ref_frame;
    uint32_t max_num_ref_frames;
    uint8_t gaps_in_frame_num_value_allowed_flag;
    uint32_t pic_width_in_mbs_minus1;
    uint32_t pic_height_in_map_units_minus1;
    uint8_t frame_mbs_only_flag;
    uint8_t mb_adaptive_frame_field_flag;
    uint8_t direct_8x8_inference_flag;
    uint8_t frame_cropping_flag;
    uint32_t frame_crop_left_offset;
    uint32_t frame_crop_right_offset;
    uint32_t frame_crop_top_offset;
    uint32_t frame_crop_bottom_offset;
    uint8_t vui_parameters_present_flag;

    uint8_t aspect_ratio_info_present_flag;
    uint8_t aspect_ratio_idc;
    uint16_t sar_width;
    uint16_t sar_height;
    uint8_t overscan_info_present_flag;
    uint8_t overscan_appropriate_flag;
    uint8_t video_signal_type_present_flag;
    uint8_t video_format;
    uint8_t video_full_range_flag;
    uint8_t color_description_present_flag;
    uint8_t color_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t chroma_loc_info_present_flag;
    uint32_t chroma_sample_loc_type_top_field;
    uint32_t chroma_sample_loc_type_bottom_field;
    uint8_t timing_info_present_flag;
    uint32_t num_units_in_tick;
    uint32_t time_scale;
    uint8_t fixed_frame_rate_flag;
    uint8_t nal_hrd_parameters_present_flag;
    uint8_t vcl_hrd_parameters_present_flag;
    uint8_t low_delay_hrd_flag;
    uint8_t pic_struct_present_flag;
    uint8_t bitstream_restriction_flag;
    uint8_t motion_vectors_over_pic_boundaries_flag;
    uint32_t max_bytes_per_pic_denom;
    uint32_t max_bits_per_mb_denom;
    uint32_t log2_max_mv_length_horizontal;
    uint32_t log2_max_mv_length_vertical;
    uint32_t max_num_reorder_frames;
    uint32_t max_dec_frame_buffering;

    uint32_t delta_scale;

    uint32_t cpb_cnt_minus1;
    uint8_t bit_rate_scale;
    uint8_t cpb_size_scale;
    uint32_t *bit_rate_value_minus1;
    uint32_t *cpb_size_value_minus1;
    uint8_t *cbr_flag;
    uint8_t initial_cpb_removal_delay_length_minus1;
    uint8_t cpb_removal_delay_length_minus1;
    uint8_t dpb_output_delay_length_minus1;
    uint8_t time_offset_length;

  // uint32_t num_views_minus1;
  // uint32_t *view_id;
  // uint32_t *num_anchhor_refs_l0;
  // uint32_t **anchor_ref_l0;
  // uint32_t *num_anchor_refs_l1;
  // uint32_t **anchhor_ref_l1;
  // uint32_t *num_non_anchor_refs_l0;
  // uint32_t **non_anchor_ref_l0;
  // uint32_t *num_non_anchor_refs_l1;
  // uint32_t **non_anchor_ref_l1;
  // uint32_t num_level_values_signalled_minus1;
  // uint32_t *num_applicable_ops_minus1;
  // uint8_t **applicable_op_temporal_id;
  // uint32_t **applicable_op_num_target_views_minus1;
  // uint32_t ***applicable_op_target_view_id;
  // uint32_t **applicable_op_num_views_minus1;
  // uint8_t mfc_format_idc;
  // uint8_t default_grid_position_flag;
  // uint8_t view0_grid_position_x;
  // uint8_t view0_grid_position_y;
  // uint8_t view1_grid_position_x;
  // uint8_t view1_grid_position_y;
} SPS_t;

SPS_t* rbsp_to_sps(RBSP_t *rbsp);

#endif

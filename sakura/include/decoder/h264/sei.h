// sei.h

#ifndef SEI_H
#define SEI_H

#include "std/common.h"

typedef struct {
  uint8_t *buffer;
  uint8_t last_payload_type_byte;
  uint8_t last_payload_size_byte;

  uint8_t seq_parameter_set_id;
  uint32_t *initial_cpb_removal_delay;
  uint32_t *initial_cpb_removal_delay_offset;

  uint32_t cpb_removal_delay;
  uint32_t dpb_output_delay;
  uint8_t pic_struct;
  uint8_t *clock_timestamp_flag;
  uint8_t ct_type;
  uint8_t nuit_field_based_flag;
  uint8_t counting_type;
  uint8_t full_timestamp_flag;
  uint8_t discontinuity_flag;
  uint8_t cnt_dropped_flag;
  uint8_t n_frames;
  uint8_t seconds_value;
  uint8_t minutes_value;
  uint8_t hours_value;
  uint8_t seconds_flag;
  uint8_t minutes_flag;
  uint8_t hours_flag;
  int32_t time_offset;

  uint32_t pan_scan_rect_id;
  uint8_t pan_scan_rect_cancel_flag;
  uint32_t pan_scan_cnt_minus1;
  int32_t *pan_scan_rect_left_offset;
  int32_t *pan_scan_rect_right_offset;
  int32_t *pan_scan_rect_top_offset;
  int32_t *pan_scan_rect_bottom_offset;
  uint32_t pan_scan_rect_repetition_period;



  uint8_t itu_t_t35_country_code;
  uint8_t itu_t_t35_country_code_extension_byte;
  uint8_t itu_t_t35_payload_byte;


  uint128_t uuid_iso_iec_11578;
  uint8_t user_data_payload_byte;

  uint32_t recovery_frame_cnt;
  uint8_t exact_match_flag;
  uint8_t broken_link_flag;
  uint8_t changing_slice_group_idc;

  uint8_t original_idr_flag;
  uint32_t original_frame_num;
  uint8_t original_field_pic_flag;
  uint8_t original_bottom_field_flag;

  uint32_t target_frame_num;
  uint8_t spare_field_flag;
  uint8_t target_bottom_field_flag;
  uint32_t num_spare_pics_minus1;
  uint32_t *delta_spare_frame_num;
  uint8_t *spare_bottom_field_flag;
  uint32_t *spare_area_idc;
  uint8_t **spare_unit_flag;
  uint32_t **zero_run_length;

  uint8_t scene_info_present_flag;
  uint32_t scene_id;
  uint32_t scene_transition_type;
  uint32_t second_scene_id;

  uint32_t sub_seq_layer_num;
  uint32_t sub_seq_id;
  uint8_t first_ref_pic_flag;
  uint8_t leading_non_ref_pic_flag;
  uint8_t last_pic_flag;
  uint8_t sub_seq_frame_num_flag;
  uint32_t sub_seq_frame_num;

  uint32_t num_sub_seq_layers_minus1;
  uint8_t accurate_statistics_flag;
  uint16_t average_bit_rate;
  uint16_t average_frame_rate;

  uint8_t duration_flag;
  uint32_t sub_seq_duration;
  uint8_t average_rate_flag;
  uint32_t num_referenced_subseqs;
  uint32_t ref_sub_seq_layer_num;
  uint32_t ref_sub_seq_id;
  uint8_t ref_sub_seq_direction;

  uint32_t full_frame_freeze_repetition_period;



  uint32_t snapshot_id;

  uint32_t progressive_refinement_id;
  uint32_t num_refinement_steps_minus1;



  uint32_t num_slice_groups_in_set_minus1;
  uint32_t *slice_group_id;
  uint8_t exact_sample_value_match_flag;
  uint8_t pan_scan_rect_flag;

  uint8_t film_grain_characteristics_cancel_flag;
  uint8_t film_grain_model_id;
  uint8_t separate_color_description_present_flag;
  uint8_t film_grain_bit_depth_luma_minus8;
  uint8_t film_grain_bit_depth_chroma_minus8;
  uint8_t film_grain_full_range_flag;
  uint8_t film_grain_color_primaries;
  uint8_t film_grain_transfer_characteristics;
  uint8_t film_grain_matrix_coefficients;
  uint8_t blending_mode_id;
  uint8_t log2_scale_factor;
  uint8_t comp_model_present_flag[3];
  uint8_t num_intensity_intervals_minus1[3];
  uint8_t num_model_values_minus1[3];
  uint8_t *intensity_interval_lower_bound[3];
  uint8_t *intensity_interval_upper_bound[3];
  uint32_t **comp_model_value[3];
  uint8_t film_grain_characteristics_repetition_period;

  uint8_t deblocking_display_preference_cancel_flag;
  uint8_t display_prior_to_deblocking_preferred_flag;
  uint8_t dec_frame_buffering_constraint_flag;
  uint32_t deblocking_display_prederence_repetition_period;

  uint8_t field_views_flag;
  uint8_t top_field_is_left_view_flag;
  uint8_t current_frame_is_left_view_flag;
  uint8_t next_frame_is_second_view_flag;
  uint8_t left_view_self_contained_flag;
  uint8_t right_view_self_contained_flag;

  uint32_t filter_hint_size_y;
  uint32_t filter_hint_size_x;
  uint8_t filter_hint_type;
  uint32_t **filter_hint[3];
  uint8_t additional_extension_flag;

  uint32_t tone_map_id;
  uint8_t tone_map_cancel_flag;
  uint32_t tone_map_repetition_period;
  uint8_t coded_data_bit_depth;
  uint8_t target_bit_depth;
  uint32_t tone_map_model_id;
  uint32_t min_value;
  uint32_t max_value;
  uint32_t sigmoid_midpoint;
  uint32_t sigmoid_width;
  uint32_t *start_of_coded_interval;
  uint16_t num_pivots;
  uint32_t *coded_pivot_value;
  uint32_t *target_pivot_value;
  uint8_t camera_iso_speed_idc;
  uint32_t camera_iso_speed_value;
  uint8_t exposure_index_idc;
  uint32_t exposure_index_value;
  uint8_t exposure_compensation_value_sign_flag;
  uint16_t exposure_compensation_value_numerator;
  uint16_t exposure_compensation_value_denom_idc;
  uint32_t ref_screen_luminance_white;
  uint32_t extended_range_white_level;
  uint16_t nominal_black_level_luma_code_value;
  uint16_t nominal_white_level_luma_code_value;
  uint16_t extended_white_level_luma_code_value;

  uint8_t reserved_sei_message_payload_byte;
} SEI_t;

#endif

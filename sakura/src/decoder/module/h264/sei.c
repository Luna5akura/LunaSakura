// sei.c

#include "std/fileio.h"
#include "std/stdio.h"
#include "util/mem.h"
#include "std/math.h"
#include "decoder/h264/nalu.h"
#include "decoder/h264/rbsp.h"
#include "decoder/h264/sps.h"
#include "decoder/h264/pps.h"
#include "decoder/h264/bitstream.h"

#include "decoder/h264/sei.h"

void free_sei(SEI_t *sei) {

}

void buffering_period(SEI_t *sei, SPS_t *sps, Bitstream *bs) {
  sei->seq_parameter_set_id = read_ue(bs);
  sei->initial_cpb_removal_delay = mmalloc(sizeof(uint32_t) * sps->cpb_cnt_minus1 + 1);
  uint8_t v = sps->initial_cpb_removal_delay_length_minus1 + 1;
  if (sps->nal_hrd_parameters_present_flag) {
    for (uint32_t SchedSelIdx = 0; SchedSelIdx < sps->cpb_cnt_minus1 + 1; SchedSelIdx++) {
      sei->initial_cpb_removal_delay[SchedSelIdx] = read_bits(bs, v);
      sei->initial_cpb_removal_delay_offset[SchedSelIdx] = read_bits(bs, v);
    }
  }
  if (sps->vcl_hrd_parameters_present_flag) {
    for (uint32_t SchedSelIdx = 0; SchedSelIdx < sps->cpb_cnt_minus1 + 1; SchedSelIdx++) {
      sei->initial_cpb_removal_delay[SchedSelIdx] = read_bits(bs, v);
      sei->initial_cpb_removal_delay_offset[SchedSelIdx] = read_bits(bs, v);
    }
  }
}

void pic_timing(SEI_t *sei, SPS_t *sps, Bitstream *bs) {
  if (sps->nal_hrd_parameters_present_flag == 1 || sps->vcl_hrd_parameters_present_flag == 1) {
    uint8_t cv = sps->cpb_removal_delay_length_minus1 + 1;
    sei->cpb_removal_delay = read_bits(bs, cv);
    uint8_t dv = sps->dpb_output_delay_length_minus1 + 1;
    sei->dpb_output_delay = read_bits(bs, dv);
  }
  if (sps->pic_struct_present_flag) {
    sei->pic_struct = read_bits(bs, 4);
    int NumClockTS;
    if (sei->pic_struct == 0 || sei->pic_struct == 1 || sei->pic_struct == 2) {
      NumClockTS = 1;
    } else if (sei->pic_struct == 3 || sei->pic_struct == 4 || sei->pic_struct == 7) {
      NumClockTS = 2;
    } else if (sei->pic_struct == 5 || sei->pic_struct == 6 || sei->pic_struct == 8) {
      NumClockTS = 3;
    } else {
      NumClockTS = 0;
    }
    sei->clock_timestamp_flag = mmalloc(sizeof(uint8_t) * NumClockTS);
    for (int i = 0; i < NumClockTS; i++) {
      sei->clock_timestamp_flag[i] = read_bit(bs);
      if (sei->clock_timestamp_flag[i]) {
        sei->ct_type = read_bits(bs, 2);
        sei->nuit_field_based_flag = read_bit(bs);
        sei->counting_type = read_bits(bs, 5);
        sei->full_timestamp_flag = read_bit(bs);
        sei->discontinuity_flag = read_bit(bs);
        sei->cnt_dropped_flag = read_bit(bs);
        sei->n_frames = read_bits(bs, 8);
        if (sei->full_timestamp_flag) {
          sei->seconds_value = read_bits(bs, 6);
          sei->minutes_value = read_bits(bs, 6);
          sei->hours_value = read_bits(bs, 5);
        } else {
          sei->seconds_flag = read_bit(bs);
          if (sei->seconds_flag) {
            sei->seconds_value = read_bits(bs, 6);
            sei->minutes_flag = read_bit(bs);
            if (sei->minutes_flag) {
              sei->minutes_value = read_bits(bs, 6);
              sei->hours_flag = read_bit(bs);
              if (sei->hours_flag) {
                sei->hours_value = read_bits(bs, 5);
              }
            }
          }
        }
        if (sps->time_offset_length > 0) {
          sei->time_offset = (int32_t)read_bits(bs, sps->time_offset_length);
        }
      }
    }
  }
}

void pan_scan_rect(SEI_t *sei, Bitstream *bs) {
  sei->pan_scan_rect_id = read_ue(bs);
  sei->pan_scan_rect_cancel_flag = read_bit(bs);
  if (!sei->pan_scan_rect_cancel_flag) {
    sei->pan_scan_cnt_minus1 = read_ue(bs);
    sei->pan_scan_rect_left_offset = mmalloc(sizeof(uint32_t) * (sei->pan_scan_cnt_minus1 + 1));
    sei->pan_scan_rect_right_offset = mmalloc(sizeof(uint32_t) * (sei->pan_scan_cnt_minus1 + 1));
    sei->pan_scan_rect_top_offset = mmalloc(sizeof(uint32_t) * (sei->pan_scan_cnt_minus1 + 1));
    sei->pan_scan_rect_bottom_offset = mmalloc(sizeof(uint32_t) * (sei->pan_scan_cnt_minus1 + 1));
    for (int i = 0; i < sei->pan_scan_cnt_minus1 + 1; i++) {
      sei->pan_scan_rect_left_offset[i] = read_se(bs);
      sei->pan_scan_rect_right_offset[i] = read_se(bs);
      sei->pan_scan_rect_top_offset[i] = read_se(bs);
      sei->pan_scan_rect_bottom_offset[i] = read_se(bs);
    }
    sei->pan_scan_rect_repetition_period = read_ue(bs);
  }
}

void filler_payload(Bitstream *bs, int payloadSize) {
  for (int k = 0; k < payloadSize; k++) {
    bs->byte_pos += 1; // 0xFF
  }
}

void user_data_registered_itu_t_t35(SEI_t *sei, Bitstream *bs, int payloadSize) {
  int i = 0;
  sei->itu_t_t35_country_code = read_bits(bs, 8);
  if (sei->itu_t_t35_country_code != 0xFF) {
    i = 1;
  } else {
    sei->itu_t_t35_country_code_extension_byte = read_bits(bs, 8);
    i = 2;
  }
  do {
    sei->itu_t_t35_payload_byte = read_bits(bs, 8);
    i++;
  } while (i < payloadSize);
}

void user_data_unregistered(SEI_t *sei, Bitstream *bs, int payloadSize) {
  sei->uuid_iso_iec_11578 = read_u128(bs);
  for (int i = 16; i < payloadSize; i++) {
    sei->user_data_payload_byte = read_bits(bs, 8);
  }
}

void recovery_point(SEI_t *sei, Bitstream *bs) {
  sei->recovery_frame_cnt = read_ue(bs);
  sei->exact_match_flag = read_bit(bs);
  sei->broken_link_flag = read_bit(bs);
  sei->changing_slice_group_idc = read_bits(bs, 2);
}

void dec_ref_pic_marking_repetition(SEI_t *sei, SPS_t *sps, Bitstream *bs) {
  sei->original_idr_flag = read_bit(bs);
  sei->original_frame_num = read_ue(bs);
  if (!sps->frame_mbs_only_flag) {
    sei->original_field_pic_flag = read_bit(bs);
    if (sei->original_field_pic_flag) {
      sei->original_bottom_field_flag = read_bit(bs);
    }
  }
  // dec_ref_pic_marking();
}

void spare_pic(SEI_t *sei, SPS_t *sps, Bitstream *bs) {
  sei->target_frame_num = read_ue(bs);
  sei->spare_field_flag = read_bit(bs);
  if (sei->spare_field_flag) {
    sei->target_bottom_field_flag = read_bit(bs);
  }
  sei->num_spare_pics_minus1 = read_ue(bs);
  uint32_t num_spare_pics = sei->num_spare_pics_minus1 + 1;
  sei->delta_spare_frame_num = mmalloc(sizeof(uint32_t) * num_spare_pics);
  sei->spare_bottom_field_flag = mmalloc(sizeof(uint8_t) * num_spare_pics);
  sei->spare_area_idc = mmalloc(sizeof(uint32_t) * num_spare_pics);
  sei->spare_unit_flag = (uint8_t **)mmalloc(sizeof(uint32_t *) * num_spare_pics);
  sei->zero_run_length = (uint32_t **)mmalloc(sizeof(uint32_t *) * num_spare_pics);
  for (int i = 0; i < sei->num_spare_pics_minus1 + 1; i++) {
    sei->delta_spare_frame_num[i] = read_ue(bs);
    uint32_t PicWidthInMbs = sps->pic_width_in_mbs_minus1 + 1;
    uint32_t PicHeightInMapUnits = sps->pic_height_in_map_units_minus1 + 1;
    uint32_t PicSizeInMapUnits = PicWidthInMbs * PicHeightInMapUnits;
    if (sei->spare_field_flag) {
      sei->spare_bottom_field_flag[i] = read_bit(bs);
    }
    sei->spare_area_idc[i] = read_ue(bs);
    sei->spare_unit_flag[i] = (uint8_t *)mmalloc(sizeof(uint8_t) * PicSizeInMapUnits);
    sei->zero_run_length[i] = (uint32_t *)mmalloc(sizeof(uint32_t) * PicSizeInMapUnits);
    if (sei->spare_area_idc[i] == 1) {
      for (uint32_t j = 0; j < PicSizeInMapUnits; j++) {
        sei->spare_unit_flag[i][j] = read_bit(bs);
      }
    } else if (sei->spare_area_idc[i] == 2) {
      uint32_t mapUnitCnt = 0;
      for (uint32_t j = 0; mapUnitCnt < PicSizeInMapUnits; j++) {
        sei->zero_run_length[i][j] = read_ue(bs);
        mapUnitCnt += sei->zero_run_length[i][j] + 1;
      }
    }
  }
}

void scene_info(SEI_t *sei, Bitstream *bs) {
  sei->scene_info_present_flag = read_bit(bs);
  if (sei->scene_info_present_flag) {
    sei->scene_id = read_ue(bs);
    sei->scene_transition_type = read_ue(bs);
    if (sei->scene_transition_type > 3) {
      sei->second_scene_id = read_ue(bs);
    }
  }
}

void sub_seq_info(SEI_t *sei, Bitstream *bs) {
  sei->sub_seq_layer_num = read_ue(bs);
  sei->sub_seq_id = read_ue(bs);
  sei->first_ref_pic_flag = read_bit(bs);
  sei->leading_non_ref_pic_flag = read_bit(bs);
  sei->last_pic_flag = read_bit(bs);
  sei->sub_seq_frame_num_flag = read_bit(bs);
  if (sei->sub_seq_frame_num_flag) {
    sei->sub_seq_frame_num = read_ue(bs);
  }
}

void sub_seq_layer_characteristics(SEI_t *sei, Bitstream *bs) {
  sei->num_sub_seq_layers_minus1 = read_ue(bs);
  for (uint32_t layer = 0; layer < sei->num_sub_seq_layers_minus1 + 1; layer++) {
    // ??????
    sei->accurate_statistics_flag = read_bit(bs);
    sei->average_bit_rate = read_bits(bs, 16);
    sei->average_frame_rate = read_bits(bs, 16);
  }
}

void sub_seq_characteristics(SEI_t *sei, Bitstream *bs) {
  sei->sub_seq_layer_num = read_ue(bs);
  sei->sub_seq_id = read_ue(bs);
  sei->duration_flag = read_bit(bs);
  if (sei->duration_flag) {
    sei->sub_seq_duration = read_bits(bs, 32);
  }
  sei->average_rate_flag = read_bit(bs);
  if (sei->average_rate_flag) {
    sei->accurate_statistics_flag = read_bit(bs);
    sei->average_bit_rate = read_bits(bs, 16);
    sei->average_frame_rate = read_bits(bs, 16);
  }
  sei->num_referenced_subseqs = read_ue(bs);
  for (uint32_t n = 0; n < sei->num_referenced_subseqs; n++) {
    // ??????
    sei->ref_sub_seq_layer_num = read_ue(bs);
    sei->ref_sub_seq_id = read_ue(bs);
    sei->ref_sub_seq_direction = read_bit(bs);
  }
}

void full_frame_freeze(SEI_t *sei, Bitstream *bs) {
  sei->full_frame_freeze_repetition_period = read_ue(bs);
}

void full_frame_freeze_release() {

}

void full_frame_snapshot(SEI_t *sei, Bitstream *bs) {
  sei->snapshot_id = read_ue(bs);
}

void progressive_refinement_segment_start(SEI_t *sei, Bitstream *bs) {
  sei->progressive_refinement_id = read_ue(bs);
  sei->num_refinement_steps_minus1 = read_ue(bs);
}

void progressive_refinement_segment_end(SEI_t *sei, Bitstream *bs) {
  sei->progressive_refinement_id = read_ue(bs);
}

void motion_constrained_slice_group_set(SEI_t *sei, PPS_t *pps, Bitstream *bs) {
  sei->num_slice_groups_in_set_minus1 = read_ue(bs);
  if (sei->num_slice_groups_in_set_minus1 > 0) {
    sei->slice_group_id = (uint32_t *)mmalloc(sizeof(uint32_t) * (sei->num_slice_groups_in_set_minus1 + 1));
    int v = ceil(log(((double)(pps->num_slice_groups_minus1 + 1)), 2));
    for (uint32_t i = 0; i < sei->num_slice_groups_in_set_minus1 + 1; i++) {
      sei->slice_group_id[i] = read_bits(bs, v);
    }
  }
  sei->exact_sample_value_match_flag = read_bit(bs);
  sei->pan_scan_rect_flag = read_bit(bs);
  if (sei->pan_scan_rect_flag) {
    sei->pan_scan_rect_id = read_ue(bs);
  }
}

void film_grain_characteristics(SEI_t *sei, Bitstream *bs) {
  sei->film_grain_characteristics_cancel_flag = read_bit(bs);
  if (!sei->film_grain_characteristics_cancel_flag) {
    sei->film_grain_model_id = read_bits(bs, 2);
    sei->separate_color_description_present_flag = read_bit(bs);
    if (sei->separate_color_description_present_flag) {
      sei->film_grain_bit_depth_luma_minus8 = read_bits(bs, 3);
      sei->film_grain_bit_depth_chroma_minus8 = read_bits(bs, 3);
      sei->film_grain_full_range_flag = read_bit(bs);
      sei->film_grain_color_primaries = read_bits(bs, 8);
      sei->film_grain_transfer_characteristics = read_bits(bs, 8);
      sei->film_grain_matrix_coefficients = read_bits(bs, 8);
    }
    sei->blending_mode_id = read_bits(bs, 2);
    sei->log2_scale_factor = read_bits(bs, 4);
    for (int c = 0; c < 3; c++) {
      sei->comp_model_present_flag[c] = read_bit(bs);
    }
    for (int c = 0; c < 3; c++) {
      if (sei->comp_model_present_flag[c]) {
        sei->num_intensity_intervals_minus1[c] = read_bits(bs, 8);
        sei->num_model_values_minus1[c] = read_bits(bs, 3);
        uint8_t imax = sei->num_intensity_intervals_minus1[c] + 1;
        sei->intensity_interval_lower_bound[c] = (uint8_t *)mmalloc(sizeof(uint8_t) * imax);
        sei->intensity_interval_upper_bound[c] = (uint8_t *)mmalloc(sizeof(uint8_t) * imax);
        sei->comp_model_value[c] = (uint32_t **)mmalloc(sizeof(uint32_t *) * imax);
        for (uint8_t i = 0; i < imax; i++) {
          sei->intensity_interval_lower_bound[c][i] = read_bits(bs, 8);
          sei->intensity_interval_upper_bound[c][i] = read_bits(bs, 8);
          sei->comp_model_value[c][i] = (uint32_t *)mmalloc(sizeof(uint32_t) * (sei->num_model_values_minus1[c] + 1));
          for (uint8_t j = 0; j < sei->num_model_values_minus1[c] + 1; j++) {
            sei->comp_model_value[c][i][j] = read_se(bs);
          }
        }
      }
    }
    sei->film_grain_characteristics_repetition_period = read_ue(bs);
  }
}

void deblocking_filter_display_preference(SEI_t *sei, Bitstream *bs) {
  sei->deblocking_display_preference_cancel_flag = read_bit(bs);
  if (!sei->deblocking_display_preference_cancel_flag) {
    sei->display_prior_to_deblocking_preferred_flag = read_bit(bs);
    sei->dec_frame_buffering_constraint_flag = read_bit(bs);
    sei->deblocking_display_prederence_repetition_period = read_ue(bs);
  }
}

void stereo_video_info(SEI_t *sei, Bitstream *bs) {
  sei->field_views_flag = read_bit(bs);
  if (sei->field_views_flag) {
    sei->top_field_is_left_view_flag = read_bit(bs);
  } else {
    sei->current_frame_is_left_view_flag = read_bit(bs);
    sei->next_frame_is_second_view_flag = rbsp_to_sps(bs);
  }
  sei->left_view_self_contained_flag = read_bit(bs);
  sei->right_view_self_contained_flag = read_bit(bs);
}

void post_filter_hint(SEI_t *sei, Bitstream *bs) {
  sei->filter_hint_size_y = read_ue(bs);
  sei->filter_hint_size_x = read_ue(bs);
  sei->filter_hint_type = read_bits(bs, 2);
  for (int color_component = 0; color_component <3; color_component++) {
    sei->filter_hint[color_component] = (uint32_t **)mmalloc(sizeof(uint32_t *) * sei->filter_hint_size_y);
    for (uint32_t cy = 0; cy < sei->filter_hint_size_y; cy++) {
      sei->filter_hint[color_component][cy] = (uint32_t *)mmalloc(sizeof(uint32_t) * sei->filter_hint_size_x);
      for (uint32_t cx = 0; cx < sei->filter_hint_size_x; cx++) {
        sei->filter_hint[color_component][cy][cx] = read_se(bs);
      }
    }
  }
  sei->additional_extension_flag = read_bit(bs);
}

void tone_mapping_info(SEI_t *sei, Bitstream *bs) {
  sei->tone_map_id = read_ue(bs);
  sei->tone_map_cancel_flag = read_bit(bs);
  if (!sei->tone_map_cancel_flag) {
    sei->tone_map_repetition_period = read_ue(bs);
    sei->coded_data_bit_depth = read_bits(bs, 8);
    sei->target_bit_depth = read_bits(bs, 8);
    sei->tone_map_model_id = read_ue(bs);
    if (sei->tone_map_model_id == 0) {
      sei->min_value = read_bits(bs, 32);
      sei->max_value = read_bits(bs, 32);
    }
    if (sei->tone_map_model_id == 1) {
      sei->sigmoid_midpoint = read_bits(bs, 32);
      sei->sigmoid_width = read_bits(bs, 32);
    }
    if (sei->tone_map_model_id == 2) {
      sei->start_of_coded_interval = (uint32_t *)mmalloc(sizeof(uint32_t) * ( 1 << sei->target_bit_depth));
      for (uint8_t i = 0; i < (1 << sei->target_bit_depth); i++) {
        sei->start_of_coded_interval[i] = read_bits(bs, ((sei->coded_data_bit_depth + 7) >> 3) << 3);
      }
    }
    if (sei->tone_map_model_id == 3) {
      sei->num_pivots = read_bits(bs, 16);
      sei->coded_pivot_value = (uint32_t *)mmalloc(sizeof(uint32_t) * sei->num_pivots);
      sei->target_pivot_value = (uint32_t *)mmalloc(sizeof(uint32_t) * sei->num_pivots);
      for (uint8_t i = 0; i < sei->num_pivots; i++) {
        sei->coded_pivot_value[i] = read_bits(bs, ((sei->coded_data_bit_depth + 7) >> 3) << 3);
        sei->target_pivot_value[i] = read_bits(bs, ((sei->target_bit_depth + 7) >> 3) << 3);
      }
    }
    if (sei->tone_map_model_id == 4) {
      sei->camera_iso_speed_idc = read_bits(bs, 8);
      if (sei->camera_iso_speed_idc == 255) {
        sei->camera_iso_speed_value = read_bits(bs, 32);
      }
      sei->exposure_index_idc = read_bits(bs, 8);
      if (sei->exposure_index_idc == 255) {
        sei->exposure_index_value = read_bits(bs, 32);
      }
      sei->exposure_compensation_value_sign_flag = read_bit(bs);
      sei->exposure_compensation_value_numerator = read_bits(bs, 16);
      sei->exposure_compensation_value_denom_idc = read_bits(bs, 16);
      sei->ref_screen_luminance_white = read_bits(bs, 32);
      sei->extended_range_white_level = read_bits(bs, 32);
      sei->nominal_black_level_luma_code_value = read_bits(bs, 16);
      sei->nominal_white_level_luma_code_value = read_bits(bs, 16);
      sei->extended_white_level_luma_code_value = read_bits(bs, 16);
    }
  }
}

void reserved_sei_message(SEI_t *sei, Bitstream *bs, int payloadSize) {
  for (int i = 0; i < payloadSize; i++) {
    sei->reserved_sei_message_payload_byte = read_bits(bs, 8);
  }
}

void sei_payload(SEI_t *sei, SPS_t *sps, PPS_t *pps, Bitstream *bs, uint8_t payloadType, int payloadSize) {
  if (payloadType == 0) {buffering_period(sei, sps, bs);}
  else if (payloadType == 1) {pic_timing(sei, sps, bs);}
  else if (payloadType == 2) {pan_scan_rect(sei, bs);}
  else if (payloadType == 3) {filler_payload(bs, payloadSize);}
  else if (payloadType == 4) {user_data_registered_itu_t_t35(sei, bs, payloadSize);}
  else if (payloadType == 5) {user_data_unregistered(sei, bs, payloadSize);}
  else if (payloadType == 6) {recovery_point(sei, bs);}
  else if (payloadType == 7) {dec_ref_pic_marking_repetition(sei, sps, bs);}
  else if (payloadType == 8) {spare_pic(sei, sps, bs);}
  else if (payloadType == 9) {scene_info(sei, bs);}
  else if (payloadType == 10) {sub_seq_info(sei, bs);}
  else if (payloadType == 11) {sub_seq_layer_characteristics(sei, bs);}
  else if (payloadType == 12) {sub_seq_characteristics(sei, bs);}
  else if (payloadType == 13) {full_frame_freeze(sei, bs);}
  else if (payloadType == 14) {full_frame_freeze_release();}
  else if (payloadType == 15) {full_frame_snapshot(sei, bs);}
  else if (payloadType == 16) {progressive_refinement_segment_start(sei, bs);}
  else if (payloadType == 17) {progressive_refinement_segment_end(sei, bs);}
  else if (payloadType == 18) {motion_constrained_slice_group_set(sei, pps, bs);}
  else if (payloadType == 19) {film_grain_characteristics(sei, bs);}
  else if (payloadType == 20) {deblocking_filter_display_preference(sei, bs);}
  else if (payloadType == 21) {stereo_video_info(sei, bs);}
  else if (payloadType == 22) {post_filter_hint(sei, bs);}
  // else if (payloadType == 23) {tone_mapping_ingo(sei, bs, payloadSize);}
  // else if (payloadType == 24) {scalability_info(sei, bs, payloadSize);}
  // else if (payloadType == 25) {sub_pic_scalable_layer(sei, bs, payloadSize);}
  // else if (payloadType == 26) {non_required_layer_rep(sei, bs, payloadSize);}
  // else if (payloadType == 27) {priority_layer_info(sei, bs, payloadSize);}
  // else if (payloadType == 28) {layers_not_present(sei, bs, payloadSize);}
  // else if (payloadType == 29) {layer_dependency_change(sei, bs, payloadSize);}
  // else if (payloadType == 30) {scalable_nesting(sei, bs, payloadSize);}
  // else if (payloadType == 31) {base_layer_temporal_hrd(sei, bs, payloadSize);}
  // else if (payloadType == 32) {quality_layer_integrity_check(sei, bs, payloadSize);}
  // else if (payloadType == 33) {redundant_pic_property(sei, bs, payloadSize);}
  // else if (payloadType == 34) {tl0_dep_rep_index(sei, bs, payloadSize);}
  // else if (payloadType == 35) {tl1_switching_poing(sei, bs, payloadSize);}
  // else if (payloadType == 36) {parallel_decoding_info(sei, bs, payloadSize);}
  // else if (payloadType == 37) {mvc_scalable_nesting(sei, bs, payloadSize);}
  // else if (payloadType == 38) {view_scalability_info(sei, bs, payloadSize);}
  // else if (payloadType == 39) {multiview_scene_info(sei, bs, payloadSize);}
  // else if (payloadType == 40) {multiview_acquision_info(sei, bs, payloadSize);}
  // else if (payloadType == 41) {non_required_view_component(sei, bs, payloadSize);}
  // else if (payloadType == 42) {view_dependency_change(sei, bs, payloadSize);}
  // else if (payloadType == 43) {operation_points_non_present(sei, bs, payloadSize);}
  // else if (payloadType == 44) {base_view_temporal_hrd(sei, bs, payloadSize);}
  // else if (payloadType == 45) {frame_packing_arrangement(sei, bs, payloadSize);}
  // else if (payloadType == 46) {multiview_view_position(sei, bs, payloadSize);}
  // else if (payloadType == 47) {display_orientation(sei, bs, payloadSize);}
  // else if (payloadType == 48) {mvcd_scalable_nesting(sei, bs, payloadSize);}
  // else if (payloadType == 49) {mvcd_view_scalability_info(sei, bs, payloadSize);}
  // else if (payloadType == 50) {depth_representation_info(sei, bs, payloadSize);}
  // else if (payloadType == 51) {three_dimensional_reference_displays_info(sei, bs, payloadSize);}
  // else if (payloadType == 52) {depth_timing(sei, bs, payloadSize);}
  // else if (payloadType == 53) {depth_sampling_info(sei, bs, payloadSize);}
  // else if (payloadType == 54) {constrained_depth_parameter_set_identifier(sei, bs, payloadSize);}
  // else if (payloadType == 56) {green_metadata(sei, bs, payloadSize);}
  // else if (payloadType == 137) {mastering_display_color_volume(sei, bs, payloadSize);}
  // else if (payloadType == 142) {color_remapping_info(sei, bs, payloadSize);}
  // else if (payloadType == 144) {content_light_level_info(sei, bs, payloadSize);}
  // else if (payloadType == 147) {alternative_transfer_characteristics(sei, bs, payloadSize);}
  // else if (payloadType == 148) {ambient_viewing_environment(sei, bs, payloadSize);}
  // else if (payloadType == 149) {content_color_volume(sei, bs, payloadSize);}
  // else if (payloadType == 150) {equirectangular_projection(sei, bs, payloadSize);}
  // else if (payloadType == 151) {cubemap_project(sei, bs, payloadSize);}
  // else if (payloadType == 154) {sphere_rotation(sei, bs, payloadSize);}
  // else if (payloadType == 155) {regionwise_packing(sei, bs, payloadSize);}
  // else if (payloadType == 156) {omni_viewport(sei, bs, payloadSize);}
  // else if (payloadType == 181) {alternative_depth_info(sei, bs, payloadSize);}
  // else if (payloadType == 200) {sei_manifest(sei, bs, payloadSize);}
  // else if (payloadType == 201) {sei_prefix_indication(sei, bs, payloadSize);}
  // else if (payloadType == 202) {annotated_regions(sei, bs, payloadSize);}
  // else if (payloadType == 205) {shutter_interval_info(sei, bs, payloadSize);}
  else {reserved_sei_message(sei, bs, payloadSize);}
  if (bs->bit_pos != 0) {
    bs->byte_pos += 1;
    bs->bit_pos = 0;
  }
}

void sei_message(SEI_t *sei, SPS_t *sps, PPS_t *pps, Bitstream *bs) {
  uint8_t payloadType = 0;
  while (((bs->data[bs->byte_pos] << bs->bit_pos) | (bs->data[bs->byte_pos + 1] >> (8 - bs->bit_pos))) == 0xFF) {
    bs->byte_pos += 1;
    payloadType += 255;
  }
  sei->last_payload_type_byte = read_bits(bs, 8);
  payloadType += sei->last_payload_type_byte;
  int payloadSize = 0;
  while ((bs->data[bs->byte_pos] << bs->bit_pos) | (bs->data[bs->byte_pos + 1] >> (8 - bs->bit_pos)) == 0xFF) {
    read_bits(bs, 8);
    payloadSize += 255;
  }
  sei->last_payload_size_byte = read_bits(bs, 8);
  payloadSize +=sei->last_payload_size_byte;
  sei_payload(sei, sps, pps, bs, payloadType, payloadSize);
}

SEI_t *rbsp_to_sei(RBSP_t *rbsp, SPS_t *sps, PPS_t *pps, SEI_t *sei) {
  sei->buffer = (uint8_t *)mmalloc(rbsp->rbsp_size);
  if (!sei->buffer) {
      free_sei(sei);
      return NULL;
  }

  Bitstream *bs = mmalloc(sizeof(Bitstream));
  bs = init_bs(bs, rbsp->buffer + 1, rbsp->rbsp_size - 1);

  do {
    sei_message(sei, sps, pps, bs);
  } while (more_rbsp_data(bs));
}

int main() {
  const char *filename = "annexb.h264";
  FILE *file = open_file(filename);
  if (!file) {
    pprintf("Error opening the file");
    return;
  }

  int nalu_count = 0;

  SPS_t *sps = mmalloc(sizeof(sps));
  PPS_t *pps = mmalloc(sizeof(pps));
  while (1) {
    NALU_t *nalu = read_annexb_nalu(file);
    if (!nalu) break;

    nalu_count++;
    pprintf("NALU #%d:\n", nalu_count);
    // print_nalu(nalu);
    pprintf("NALU #%d size: %d bytes\n\n", nalu_count, nalu->nalu_size);

    RBSP_t *rbsp = nalu_to_rbsp(nalu);
    if (rbsp->nal_unit_type == 7) {
        sps = rbsp_to_sps(rbsp);
    } else if (rbsp->nal_unit_type == 8) {
      pps = rbsp_to_pps(rbsp, sps);
    } else if (rbsp->nal_unit_type == 6) {
      SEI_t *sei = mmalloc(sizeof(sei));
      sei = rbsp_to_sei(rbsp, sps, pps, sei);
    }

    print_rbsp(rbsp);

    free_nalu(nalu);
  }
  close_file(file);
}

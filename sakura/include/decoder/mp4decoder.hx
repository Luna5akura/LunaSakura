// mp4encoder.h

#ifndef MP4ENCODER_H
#define MP4ENCODER_H

#include "std/common.h"

#define MAX_REF_FRAMES 16

#define MEDIA_TYPE_VIDEO 1
#define MEDIA_TYPE_AUDIO 2



typedef struct {
  uint8_t forbidden_zero_bit;
  uint8_t nal_ref_idc;
  uint8_t nal_unit_type;
  uint8_t *rbsp;
  uint32_t rbsp_size;
} NALU_t;

typedef struct {
  uint8_t *data;
  uint32_t size;
  uint32_t byte_pos;
  uint8_t bit_pos;
} Bitstream;

typedef struct {
    uint8_t profile_idc;
    uint8_t constraint_set_flags;
    uint8_t level_idc;
    uint32_t seq_parameter_set_id;
    uint32_t log2_max_frame_num_minus4;
    uint32_t pic_order_cnt_type;
    uint32_t log2_max_pic_order_cnt_lsb_minus4;
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
    uint32_t chroma_format_idc;
    uint8_t separate_colour_plane_flag;
    uint8_t delta_pic_order_always_zero_flag;
    int32_t offset_for_non_ref_pic;
    int32_t offset_for_top_to_bottom_field;
    uint32_t num_ref_frames_in_pic_order_cnt_cycle;
} SPS_t;


typedef struct {
    uint32_t pic_parameter_set_id;
    uint32_t seq_parameter_set_id;
    uint8_t entropy_coding_mode_flag;
    uint8_t bottom_field_pic_order_in_frame_present_flag;
    uint32_t num_slice_groups_minus1;
    uint32_t num_ref_idx_l0_default_active_minus1;
    uint32_t num_ref_idx_l1_default_active_minus1;
    uint8_t weighted_pred_flag;
    uint8_t weighted_bipred_idc;
    int32_t pic_init_qp_minus26;
    int32_t pic_init_qs_minus26;
    int32_t chroma_qp_index_offset;
    uint8_t deblocking_filter_control_present_flag;
    uint8_t constrained_intra_pred_flag;
    uint8_t redundant_pic_cnt_present_flag;
} PPS_t;



typedef struct {
    uint32_t first_mb_in_slice;
    uint32_t slice_type;
    uint32_t pic_parameter_set_id;
    uint32_t frame_num;
    uint32_t pic_order_cnt_lsb;
    int32_t slice_qp_delta;
    int32_t slice_qp;
    uint8_t nal_unit_type;
    uint8_t field_pic_flag;
    uint8_t bottom_field_flag;
    uint32_t idr_pic_id;
    int32_t delta_pic_order_cnt[2];
    uint32_t redundant_pic_cnt;
    uint8_t direct_spatial_mv_pred_flag;
    uint8_t num_ref_idx_active_override_flag;
    uint32_t num_ref_idx_l0_active_minus1;
    uint32_t num_ref_idx_l1_active_minus1;
} SliceHeader_t;


typedef struct {
  uint32_t width;
  uint32_t height;
  uint8_t *data_y;
  uint8_t *data_u;
  uint8_t *data_v;
} Frame;

typedef struct {
  Frame *frames[MAX_REF_FRAMES];
  int current_index;
  Frame *ref_list0[MAX_REF_FRAMES];
  Frame *ref_list1[MAX_REF_FRAMES];
  int ref_count0;
  int ref_count1;
} FrameBuffer;

typedef struct {
  uint32_t sample_count;
  uint32_t *sample_sizes;
} SampleSizeTable;

typedef struct {
  uint32_t chunk_count;
  uint64_t *chunk_offsets;
  uint64_t *offsets;
} ChunkOffsetTable;

typedef struct {
  uint32_t entry_count;
  uint32_t *first_chunk;
  uint32_t *samples_per_chunk;
  uint32_t *sample_description_index;
} SampleToChunkTable;

typedef struct {
  uint32_t sample_count;
  uint32_t *sample_durations;
} TimeSampleTable;

typedef struct {
  uint32_t mb_type;
  int is_intra;
  int intra4x4_pred_mode[16];
  uint32_t intra16x16_pred_mode;
  uint8_t pcm_samples[256];
  uint32_t coded_block_pattern;
  int32_t qp;
} Macroblock;

typedef struct {
  SampleSizeTable size_table;
  ChunkOffsetTable offset_table;
  SampleToChunkTable stsc_table;
  TimeSampleTable time_table;
  uint32_t media_type;
  uint64_t mdat_offset;
  uint64_t mdat_size;

  uint8_t **sps_list;
  uint16_t *sps_length;
  uint8_t sps_count;

  uint8_t **pps_list;
  uint8_t *pps_length;
  uint8_t pps_count;

  uint8_t *audio_specific_config;
  uint8_t audio_specific_config_size;
} Track;

#endif

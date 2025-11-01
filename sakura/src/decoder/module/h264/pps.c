// pps.c

#include "std/stdio.h"
#include "std/fileio.h"
#include "std/math.h"
#include "util/mem.h"
#include "decoder/h264/nalu.h"
#include "decoder/h264/rbsp.h"
#include "decoder/h264/bitstream.h"
#include "decoder/h264/sps.h"

#include "decoder/h264/pps.h"


void free_pps(PPS_t *pps) {
}

PPS_t* rbsp_to_pps(RBSP_t *rbsp, SPS_t *sps) {
  PPS_t *pps = mmalloc(sizeof(PPS_t));
  pps->buffer = (uint8_t *)mmalloc(rbsp->rbsp_size);
  if (!pps->buffer) {
      free_pps(pps);
      return NULL;
  }

  Bitstream* bs = mmalloc(sizeof(Bitstream));
  bs = init_bs(bs, rbsp->buffer + 1, rbsp->rbsp_size - 1);

  pps->pic_parameter_set_id = read_ue(bs);
  pps->seq_parameter_set_id = read_ue(bs);
  pps->entropy_coding_mode_flag = read_bit(bs);
  pps->bottom_field_pic_order_in_frame_present_flag = read_bit(bs);
  pps->num_slice_groups_minus1 = read_ue(bs);
  uint32_t num_slice_group = pps->num_slice_groups_minus1 + 1;
  pps->slice_group_map_type = read_ue(bs);
  pps->run_length_minus1 = (uint32_t *)mmalloc(sizeof(uint32_t) * num_slice_group);
  if (num_slice_group > 1) {
    pps->slice_group_map_type = read_ue(bs);
    if (pps->slice_group_map_type == 0) {
      for (uint32_t iGroup = 0; iGroup < num_slice_group; iGroup++) {
        pps->run_length_minus1[iGroup] = read_ue(bs);
      }
    } else if (pps->slice_group_map_type == 2) {
      pps->top_left = (uint32_t *)mmalloc(sizeof(uint32_t) * num_slice_group);
      pps->bottom_right = (uint32_t *)mmalloc(sizeof(uint32_t) * num_slice_group);
      for (uint32_t iGroup = 0; iGroup < num_slice_group; iGroup++) {
        pps->top_left[iGroup] = read_ue(bs);
        pps->bottom_right[iGroup] = read_ue(bs);
      }
    } else if (pps->slice_group_map_type == 3
      || pps->slice_group_map_type == 4 || pps->slice_group_map_type == 5) {
      pps->slice_group_change_direction_flag == read_bit(bs);
      pps->slice_group_change_rate_minus_1 == read_ue(bs);
    } else if (pps->slice_group_map_type == 6) {
      pps->pic_size_in_map_units_minus1 == read_ue(bs);
      pps->slice_group_id = (uint32_t *)mmalloc(sizeof(uint32_t) * pps->pic_size_in_map_units_minus1 + 1);
      for (uint32_t i = 0; i < pps->pic_size_in_map_units_minus1 + 1; i++) {
        pps->slice_group_id[i] = read_bits(bs, ceil((double)log((double)(pps->num_slice_groups_minus1 + 1), 2)));
      }
    }
  }
  pps->num_ref_idx_l0_default_active_minus1 = read_ue(bs);
  pps->num_ref_idx_l1_default_active_minus1 = read_ue(bs);
  pps->weighted_pred_flag = read_bit(bs);
  pps->weighted_bipred_idc = read_bits(bs, 2);
  pps->pic_init_qp_minus26 = read_se(bs);
  pps->pic_init_qs_minus26 = read_se(bs);
  pps->chroma_qp_index_offset = read_se(bs);
  pps->deblocking_filter_control_present_flag = read_bit(bs);
  pps->constrained_intra_pred_flag = read_bit(bs);
  pps->redundant_pic_cnt_present_flag = read_bit(bs);
  if (more_rbsp_data(bs)) {
    pps->transform_8x8_mode_flag = read_bit(bs);
    pps->pic_scaling_matrix_present_flag = read_bit(bs);
    if (pps->pic_scaling_matrix_present_flag) {
      int pic_scaling_list_present_cnt = (6 + ((sps->chroma_format_idc) != 3) ? 2 : 6) * pps->transform_8x8_mode_flag;
      for (int i = 0; i < pic_scaling_list_present_cnt; i++) {
        pps->pic_scaling_list_present_flag[i] = read_bit(bs);
        if (pps->pic_scaling_list_present_flag[i]) {
          if (i < 6) {
            //
          } else {
            //
          }
        }
      }
    }
    pps->second_chroma_qp_index_offset == read_se(bs);
  }
  return pps;
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
//   SPS_t *sps = mmalloc(sizeof(sps));
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
//         sps = rbsp_to_sps(rbsp);
//     } else if (rbsp->nal_unit_type == 8) {
//       PPS_t *pps = mmalloc(sizeof(pps));
//       pps = rbsp_to_pps(rbsp, sps);
//     }
//
//     print_rbsp(rbsp);
//
//     free_nalu(nalu);
//   }
//   close_file(file);
// }

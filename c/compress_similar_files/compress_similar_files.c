/* Copyright 2020 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#include <brotli/encode.h>
#include <brotli/decode.h>
#include <stdlib.h>
#include <stdio.h>

int DEFAULT_WINDOW = 24;
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

int MinWindowLargerThanFile(int fileSize, int max) {
    int window = 24;
    if (fileSize > 0) {
        window = 10;
        while (((size_t)1 << (window)) - 16 < (uint64_t)fileSize) {
            ++window;
            if (window == max) break;
        }
    }
    return window;
}

size_t RemoveBackwardReferencesPart(const BackwardReferenceFromDecoder* backward_references,
                                    int backward_references_size,
                                    int start, int end,
                                    BackwardReferenceFromDecoder** new_backward_references,
                                    int lgwin) {
  int new_position, new_copy_len, new_distance, new_max_distance;
  int max_distance;
  int max_backward_limit = (((size_t)1 << (lgwin)) - 16);
  *new_backward_references = (BackwardReferenceFromDecoder*)malloc(sizeof(BackwardReferenceFromDecoder) * backward_references_size);
  int new_back_ref_size = 0;
  for (int i = 0; i < backward_references_size; ++i) {
    BackwardReferenceFromDecoder ref = backward_references[i];
    max_distance = MIN(ref.position, max_backward_limit);
    max_distance = ref.max_distance;
    /* Reference to static dictionary */
    if (ref.distance > max_distance) {
      if (ref.position < start) {
        *(*new_backward_references + new_back_ref_size) = ref;
        new_back_ref_size++;
      } else if (ref.position >= end) {
        new_position = ref.position - (end - start);
        new_max_distance = MIN(new_position, ref.max_distance);
        *(*new_backward_references + new_back_ref_size) = (BackwardReferenceFromDecoder){new_position, ref.copy_len, ref.distance, new_max_distance};
        new_back_ref_size++;
      }
    }
    /* Backward reference */
    else {
      /* If it starts before removing content then look at where it ends */
      if (ref.position < start) {
        if (ref.position + ref.copy_len < start) {
          *(*new_backward_references + new_back_ref_size) = ref;
          new_back_ref_size++;
        } else {
          /* Cut the substring and save the new copy len >= 3 */
          if (start - ref.position >= 3) {
            new_copy_len = start - ref.position;
            *(*new_backward_references + new_back_ref_size) =
                 (BackwardReferenceFromDecoder){ref.position, new_copy_len,
                                                ref.distance, ref.max_distance};
            new_back_ref_size++;
          }
        }
      }
      /* If it's after removing content then look at
         where backward reference goes */
      else if (ref.position >= end) {
        /* If the equal substring starts inside removing content
           the cut it and save if new copy len is long enough */
        if (ref.position - ref.distance >= start && ref.position - ref.distance < end) {
          if (ref.position - ref.distance + ref.copy_len - 1 >= end + 5) {
            new_position = end + ref.distance - (end - start);
            new_distance = ref.distance;
            new_copy_len = ref.copy_len - (end - ref.position + ref.distance);
            new_max_distance = MIN(ref.max_distance, new_position);
            *(*new_backward_references + new_back_ref_size) = (BackwardReferenceFromDecoder){new_position, new_copy_len, new_distance, new_max_distance};
            new_back_ref_size++;
          }
        }
        /* If the equal substring starts before removing content
           then look where it ends */
        else if (ref.position - ref.distance < start) {
          if (ref.position - ref.distance + ref.copy_len - 1 < start) {
            new_distance = ref.distance - (end - start);
            new_position = ref.position - (end - start);
            new_max_distance = MIN(ref.max_distance, new_position);
            *(*new_backward_references + new_back_ref_size) = (BackwardReferenceFromDecoder){new_position, ref.copy_len, new_distance, new_max_distance};
            new_back_ref_size++;
          }
          /* If the end is after removing content then cut the end
             and save if the new copy len is long enough */
          else {
            new_position = ref.position - (end - start);
            new_distance = ref.distance - (end - start);
            new_copy_len = start - (ref.position - ref.distance);
            new_max_distance = MIN(ref.max_distance, new_position);
            if (new_copy_len >= 3) {
              *(*new_backward_references + new_back_ref_size) = (BackwardReferenceFromDecoder){new_position, new_copy_len, new_distance, new_max_distance};
              new_back_ref_size++;
            }
          }
        }
        /* If the equal substring starts after removing content
           then just save it */
        else {
          new_position = ref.position - (end - start);
          new_max_distance = MIN(ref.max_distance, new_position);
          *(*new_backward_references + new_back_ref_size) = (BackwardReferenceFromDecoder){new_position, ref.copy_len, ref.distance, new_max_distance};
          new_back_ref_size++;
        }
      }
    }
  }
  return new_back_ref_size;
}

/* Saves a block <pos_begin, pos_end, old_type> into new_block_splits*/
void SaveNewBlock(size_t pos_begin, size_t pos_end, uint8_t old_type,
                  int* types_mapping, BlockSplitFromDecoder* new_block_splits,
                  size_t* new_num_blocks, size_t* new_num_types) {
  /* Haven't seen this type before, save a mapping */
  if (types_mapping[old_type] == -1) {
    types_mapping[old_type] = *new_num_types;
  }
  /* Create a new block */
  if (*new_num_blocks == 0 || (new_block_splits->types[*new_num_blocks - 1] != types_mapping[old_type] && pos_end - pos_begin > 3)) {
    new_block_splits->positions_begin[*new_num_blocks] = pos_begin;
    new_block_splits->positions_end[*new_num_blocks] = pos_end;
    new_block_splits->types[*new_num_blocks] = types_mapping[old_type];
    *new_num_types = MAX(*new_num_types, (size_t)new_block_splits->types[*new_num_blocks] + 1);
    (*new_num_blocks)++;
  } else {
    new_block_splits->positions_end[*new_num_blocks - 1] = pos_end;
  }
}

void RemoveBlockSplittingPart(const BlockSplitFromDecoder* block_splits,
                              int start, int end,
                              BlockSplitFromDecoder* new_block_splits) {
  size_t new_num_blocks = 0;
  size_t new_num_types = 0;
  int* types_mapping = (int*)malloc(sizeof(int) * block_splits->num_types);
  /* Mapping of the types from decoder (they increase with each metablock)
     to the appropriate types */
  for (int i = 0; i < block_splits->num_types; ++i) {
    types_mapping[i] = -1;
  }
  new_block_splits->types = (uint8_t*)malloc(sizeof(uint8_t) * block_splits->num_blocks);
  new_block_splits->positions_begin = (uint32_t*)malloc(sizeof(uint32_t) * block_splits->num_blocks);
  new_block_splits->positions_end = (uint32_t*)malloc(sizeof(uint32_t) * block_splits->num_blocks);
  new_block_splits->positions_alloc_size = block_splits->num_blocks;
  new_block_splits->types_alloc_size = block_splits->num_blocks;
  size_t pos_begin, pos_end;
  for (int i = 0; i < block_splits->num_blocks; ++i) {
    pos_begin = block_splits->positions_begin[i];
    pos_end = block_splits->positions_end[i];
    if (pos_begin < start) {
      if (pos_end <= start) {
        /* If current block lies before removing content then save it */
        SaveNewBlock(pos_begin, pos_end, block_splits->types[i], types_mapping,
                     new_block_splits, &new_num_blocks, &new_num_types);
      } else if (start < pos_end && pos_end <= end) {
        /* If current block starts before removing content but ends inside
           then cut the end of the current block */
        SaveNewBlock(pos_begin, start, block_splits->types[i], types_mapping,
                     new_block_splits, &new_num_blocks, &new_num_types);
      } else {
        /* If current block starts before removing content but ends after
           then then cut the middle of the current block */
        SaveNewBlock(pos_begin, pos_end - (end - start), block_splits->types[i],
                     types_mapping, new_block_splits, &new_num_blocks, &new_num_types);
      }
    } else if (pos_begin < end) {
      if (pos_end > end) {
        /* If current block starts inside removing content but ends after
           then cut the beginning of the current block */
        SaveNewBlock(start, start + (pos_end - end), block_splits->types[i],
                     types_mapping, new_block_splits, &new_num_blocks, &new_num_types);
      }
    } else {
      /* If current block starts after removing content (=> ends after)
         then save it*/
      SaveNewBlock(pos_begin - (end - start), pos_end - (end - start), block_splits->types[i],
                   types_mapping, new_block_splits, &new_num_blocks, &new_num_types);
    }
  }
  new_block_splits->num_blocks = new_num_blocks;
  new_block_splits->num_types = new_num_types;
}


void RemoveDataPart(const unsigned char* input_data,
                    size_t input_size,
                    int start, int end,
                    unsigned char** output_data,
                    size_t* output_data_size,
                    const BackwardReferenceFromDecoder* backward_references,
                    size_t backward_references_size,
                    BackwardReferenceFromDecoder** new_backward_references,
                    size_t* new_backward_references_size,
                    const BlockSplitFromDecoder* literals_block_splits,
                    BlockSplitFromDecoder* new_literals_block_splits,
                    const BlockSplitFromDecoder* cmds_block_splits,
                    BlockSplitFromDecoder* new_cmds_block_splits) {

  /* Delete part of the input_data and save it to output_data */
  size_t output_idx = 0;
  for (int i = 0; i < input_size; ++i) {
    if (i < start || i >= end) {
      (*output_data)[output_idx] = input_data[i];
      output_idx++;
    }
  }
  *output_data_size = output_idx;

  /* Adjust backward references to the new data */
  int window = MinWindowLargerThanFile(input_size, DEFAULT_WINDOW);
  *new_backward_references_size = RemoveBackwardReferencesPart(
                                backward_references, backward_references_size,
                                start, end, new_backward_references, window);
  /* Adjust block splits to the new data */
  RemoveBlockSplittingPart(literals_block_splits, start, end,
                           new_literals_block_splits);
  RemoveBlockSplittingPart(cmds_block_splits, start, end,
                           new_cmds_block_splits);
}

/**
 * Compress file which is got by deleting some part of another file
 *
 * input_buffer - compressed file
 * start, end - start and end indeces in the initial uncompressed file
 *  (decompressed input_buffer) which specify what part should be deleted
 *
 */
BROTLI_BOOL BrotliEncoderCompressSimilarDeletion(
    int quality, int lgwin, BrotliEncoderMode mode, size_t input_size,
    const uint8_t* input_buffer, int start, int end, size_t* encoded_size,
    uint8_t* encoded_buffer) {
  size_t decompressed_buffer_size = input_size * 100;
  unsigned char* decompressed_data = (unsigned char*) malloc(decompressed_buffer_size);
  BlockSplitFromDecoder literals_block_splits;
  BlockSplitFromDecoder insert_copy_length_block_splits;
  BackwardReferenceFromDecoder* backward_references;
  size_t back_refs_size;
  /* Decompress input_buffer and
     save all the information needed for recompression*/
  if (BrotliDecoderDecompress(input_size, input_buffer, &decompressed_buffer_size, decompressed_data,
                              BROTLI_TRUE, &backward_references, &back_refs_size,
                              &literals_block_splits, &insert_copy_length_block_splits) != 1) {
    printf("Failure in BrotliDecompress\n");
  }

  /* Delete file part and adjust backward references
     and block splits to a new file */
  BackwardReferenceFromDecoder* new_backward_references = NULL;
  size_t new_backward_references_size;
  BlockSplitFromDecoder new_literals_block_splits;
  BlockSplitFromDecoder new_insert_copy_length_block_splits;
  unsigned char* removed_data = (unsigned char*) malloc(decompressed_buffer_size);
  size_t removed_data_size = 0;
  RemoveDataPart(decompressed_data, decompressed_buffer_size,
                 start, end, &removed_data, &removed_data_size,
                 backward_references, back_refs_size,
                 &new_backward_references, &new_backward_references_size,
                 &literals_block_splits, &new_literals_block_splits,
                 &insert_copy_length_block_splits,
                 &new_insert_copy_length_block_splits);

  /* Compress new file with use if collected information*/
  lgwin = MinWindowLargerThanFile(removed_data_size, DEFAULT_WINDOW);
  return BrotliEncoderCompress(quality, lgwin, mode,
                               removed_data_size, removed_data,
                               encoded_size, encoded_buffer,
                               &new_backward_references,
                               new_backward_references_size,
                               &new_literals_block_splits,
                               &new_insert_copy_length_block_splits);
}

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

// Function to find backward reference copies.

#include "./backward_references.h"

#include <algorithm>
#include <limits>
#include <vector>

#include "./command.h"
#include "./fast_log.h"
#include "./literal_cost.h"

namespace brotli {

// The maximum length for which the zopflification uses distinct distances.
static const uint16_t kMaxZopfliLen = 325;

static const double kInfinity = std::numeric_limits<double>::infinity();

// Histogram based cost model for zopflification.
class ZopfliCostModel {
 public:
  ZopfliCostModel() : min_cost_cmd_(kInfinity) {}

  void SetFromCommands(size_t num_bytes,
                       size_t position,
                       const uint8_t* ringbuffer,
                       size_t ringbuffer_mask,
                       const Command* commands,
                       size_t num_commands,
                       size_t last_insert_len) {
    std::vector<uint32_t> histogram_literal(256, 0);
    std::vector<uint32_t> histogram_cmd(kNumCommandPrefixes, 0);
    std::vector<uint32_t> histogram_dist(kNumDistancePrefixes, 0);

    size_t pos = position - last_insert_len;
    for (size_t i = 0; i < num_commands; i++) {
      size_t inslength = commands[i].insert_len_;
      size_t copylength = commands[i].copy_len_;
      size_t distcode = commands[i].dist_prefix_;
      size_t cmdcode = commands[i].cmd_prefix_;

      histogram_cmd[cmdcode]++;
      if (cmdcode >= 128) histogram_dist[distcode]++;

      for (size_t j = 0; j < inslength; j++) {
        histogram_literal[ringbuffer[(pos + j) & ringbuffer_mask]]++;
      }

      pos += inslength + copylength;
    }

    std::vector<double> cost_literal;
    Set(histogram_literal, &cost_literal);
    Set(histogram_cmd, &cost_cmd_);
    Set(histogram_dist, &cost_dist_);

    for (uint32_t i = 0; i < kNumCommandPrefixes; ++i) {
      min_cost_cmd_ = std::min(min_cost_cmd_, cost_cmd_[i]);
    }

    literal_costs_.resize(num_bytes + 1);
    literal_costs_[0] = 0.0;
    for (size_t i = 0; i < num_bytes; ++i) {
      literal_costs_[i + 1] = literal_costs_[i] +
          cost_literal[ringbuffer[(position + i) & ringbuffer_mask]];
    }
  }

  void SetFromLiteralCosts(size_t num_bytes,
                           size_t position,
                           const uint8_t* ringbuffer,
                           size_t ringbuffer_mask) {
    std::vector<float> literal_cost(num_bytes + 1);
    EstimateBitCostsForLiterals(position, num_bytes, ringbuffer_mask,
                                ringbuffer, &literal_cost[0]);
    literal_costs_.resize(num_bytes + 1);
    literal_costs_[0] = 0.0;
    for (size_t i = 0; i < num_bytes; ++i) {
      literal_costs_[i + 1] = literal_costs_[i] + literal_cost[i];
    }
    cost_cmd_.resize(kNumCommandPrefixes);
    cost_dist_.resize(kNumDistancePrefixes);
    for (uint32_t i = 0; i < kNumCommandPrefixes; ++i) {
      cost_cmd_[i] = FastLog2(11 + i);
    }
    for (uint32_t i = 0; i < kNumDistancePrefixes; ++i) {
      cost_dist_[i] = FastLog2(20 + i);
    }
    min_cost_cmd_ = FastLog2(11);
  }

  double GetCommandCost(
      size_t dist_code, size_t length_code, size_t insert_length) const {
    uint16_t inscode = GetInsertLengthCode(insert_length);
    uint16_t copycode = GetCopyLengthCode(length_code);
    uint16_t cmdcode = CombineLengthCodes(inscode, copycode, dist_code == 0);
    uint16_t dist_symbol;
    uint32_t distextra;
    PrefixEncodeCopyDistance(dist_code, 0, 0, &dist_symbol, &distextra);
    uint32_t distnumextra = distextra >> 24;

    double result =  static_cast<double>(
        kInsExtra[inscode] + kCopyExtra[copycode] + distnumextra);
    result += cost_cmd_[cmdcode];
    if (cmdcode >= 128) result += cost_dist_[dist_symbol];
    return result;
  }

  double GetLiteralCosts(size_t from, size_t to) const {
    return literal_costs_[to] - literal_costs_[from];
  }

  double GetMinCostCmd() const {
    return min_cost_cmd_;
  }

 private:
  void Set(const std::vector<uint32_t>& histogram, std::vector<double>* cost) {
    cost->resize(histogram.size());
    size_t sum = 0;
    for (size_t i = 0; i < histogram.size(); i++) {
      sum += histogram[i];
    }
    double log2sum = FastLog2(sum);
    for (size_t i = 0; i < histogram.size(); i++) {
      if (histogram[i] == 0) {
        (*cost)[i] = log2sum + 2;
        continue;
      }

      // Shannon bits for this symbol.
      (*cost)[i] = log2sum - FastLog2(histogram[i]);

      // Cannot be coded with less than 1 bit
      if ((*cost)[i] < 1) (*cost)[i] = 1;
    }
  }

  std::vector<double> cost_cmd_;  // The insert and copy length symbols.
  std::vector<double> cost_dist_;
  // Cumulative costs of literals per position in the stream.
  std::vector<double> literal_costs_;
  double min_cost_cmd_;
};

inline void SetDistanceCache(size_t distance,
                             size_t distance_code,
                             size_t max_distance,
                             const int* dist_cache,
                             int* result_dist_cache) {
  if (distance <= max_distance && distance_code > 0) {
    result_dist_cache[0] = static_cast<int>(distance);
    memcpy(&result_dist_cache[1], dist_cache, 3 * sizeof(dist_cache[0]));
  } else {
    memcpy(result_dist_cache, dist_cache, 4 * sizeof(dist_cache[0]));
  }
}

inline size_t ComputeDistanceCode(size_t distance,
                                  size_t max_distance,
                                  int quality,
                                  const int* dist_cache) {
  if (distance <= max_distance) {
    if (distance == static_cast<size_t>(dist_cache[0])) {
      return 0;
    } else if (distance == static_cast<size_t>(dist_cache[1])) {
      return 1;
    } else if (distance == static_cast<size_t>(dist_cache[2])) {
      return 2;
    } else if (distance == static_cast<size_t>(dist_cache[3])) {
      return 3;
    } else if (quality > 3 && distance >= 6) {
      for (size_t k = 4; k < kNumDistanceShortCodes; ++k) {
        size_t idx = kDistanceCacheIndex[k];
        size_t candidate =
            static_cast<size_t>(dist_cache[idx] + kDistanceCacheOffset[k]);
        static const size_t kLimits[16] = {  0,  0,  0,  0,
                                             6,  6, 11, 11,
                                            11, 11, 11, 11,
                                            12, 12, 12, 12 };
        if (distance == candidate && distance >= kLimits[k]) {
          return k;
        }
      }
    }
  }
  return distance + 15;
}

struct ZopfliNode {
  ZopfliNode() : length(1),
                 distance(0),
                 distance_code(0),
                 length_code(0),
                 insert_length(0),
                 cost(kInfinity) {}

  // best length to get up to this byte (not including this byte itself)
  uint32_t length;
  // distance associated with the length
  uint32_t distance;
  uint32_t distance_code;
  int distance_cache[4];
  // length code associated with the length - usually the same as length,
  // except in case of length-changing dictionary transformation.
  uint32_t length_code;
  // number of literal inserts before this copy
  uint32_t insert_length;
  // smallest cost to get to this byte from the beginning, as found so far
  double cost;
};

inline void UpdateZopfliNode(ZopfliNode* nodes, size_t pos, size_t start_pos,
                             size_t len, size_t len_code, size_t dist,
                             size_t dist_code, size_t max_dist,
                             const int* dist_cache, double cost) {
  ZopfliNode& next = nodes[pos + len];
  next.length = static_cast<uint32_t>(len);
  next.length_code = static_cast<uint32_t>(len_code);
  next.distance = static_cast<uint32_t>(dist);
  next.distance_code = static_cast<uint32_t>(dist_code);
  next.insert_length = static_cast<uint32_t>(pos - start_pos);
  next.cost = cost;
  SetDistanceCache(dist, dist_code, max_dist, dist_cache,
                   &next.distance_cache[0]);
}

// Maintains the smallest 2^k cost difference together with their positions
class StartPosQueue {
 public:
  explicit StartPosQueue(int bits)
      : mask_((1u << bits) - 1), q_(1 << bits), idx_(0) {}

  void Clear() {
    idx_ = 0;
  }

  void Push(size_t pos, double costdiff) {
    if (costdiff == kInfinity) {
      // We can't start a command from an unreachable start position.
      // E.g. position 1 in a stream is always unreachable, because all commands
      // have a copy of at least length 2.
      return;
    }
    size_t offset = -idx_ & mask_;
    ++idx_;
    size_t len = size();
    q_[offset] = std::make_pair(pos, costdiff);
    /* Restore the sorted order. In the list of |len| items at most |len - 1|
       adjacent element comparisons / swaps are required. */
    for (size_t i = 1; i < len; ++i) {
      if (q_[offset & mask_].second > q_[(offset + 1) & mask_].second) {
        std::swap(q_[offset & mask_], q_[(offset + 1) & mask_]);
      }
      ++offset;
    }
  }

  size_t size() const { return std::min(idx_, mask_ + 1); }

  size_t GetStartPos(size_t k) const {
    return q_[(k + 1 - idx_) & mask_].first;
  }

 private:
  const size_t mask_;
  std::vector<std::pair<size_t, double> > q_;
  size_t idx_;
};

// Returns the minimum possible copy length that can improve the cost of any
// future position.
size_t ComputeMinimumCopyLength(const StartPosQueue& queue,
                                const std::vector<ZopfliNode>& nodes,
                                const ZopfliCostModel& model,
                                size_t pos,
                                double min_cost_cmd) {
  // Compute the minimum possible cost of reaching any future position.
  const size_t start0 = queue.GetStartPos(0);
  double min_cost = (nodes[start0].cost +
                     model.GetLiteralCosts(start0, pos) +
                     min_cost_cmd);
  size_t len = 2;
  size_t next_len_bucket = 4;
  size_t next_len_offset = 10;
  while (pos + len < nodes.size() && nodes[pos + len].cost <= min_cost) {
    // We already reached (pos + len) with no more cost than the minimum
    // possible cost of reaching anything from this pos, so there is no point in
    // looking for lengths <= len.
    ++len;
    if (len == next_len_offset) {
      // We reached the next copy length code bucket, so we add one more
      // extra bit to the minimum cost.
      min_cost += 1.0;
      next_len_offset += next_len_bucket;
      next_len_bucket *= 2;
    }
  }
  return len;
}

void ZopfliIterate(size_t num_bytes,
                   size_t position,
                   const uint8_t* ringbuffer,
                   size_t ringbuffer_mask,
                   const size_t max_backward_limit,
                   const ZopfliCostModel& model,
                   const std::vector<uint32_t>& num_matches,
                   const std::vector<BackwardMatch>& matches,
                   int* dist_cache,
                   size_t* last_insert_len,
                   Command* commands,
                   size_t* num_commands,
                   size_t* num_literals) {
  const Command * const orig_commands = commands;

  std::vector<ZopfliNode> nodes(num_bytes + 1);
  nodes[0].length = 0;
  nodes[0].cost = 0;
  memcpy(nodes[0].distance_cache, dist_cache, 4 * sizeof(dist_cache[0]));

  StartPosQueue queue(3);
  const double min_cost_cmd = model.GetMinCostCmd();

  size_t cur_match_pos = 0;
  for (size_t i = 0; i + 3 < num_bytes; i++) {
    size_t cur_ix = position + i;
    size_t cur_ix_masked = cur_ix & ringbuffer_mask;
    size_t max_distance = std::min(cur_ix, max_backward_limit);
    size_t max_length = num_bytes - i;

    queue.Push(i, nodes[i].cost - model.GetLiteralCosts(0, i));

    const size_t min_len = ComputeMinimumCopyLength(queue, nodes, model,
                                                    i, min_cost_cmd);

    // Go over the command starting positions in order of increasing cost
    // difference.
    for (size_t k = 0; k < 5 && k < queue.size(); ++k) {
      const size_t start = queue.GetStartPos(k);
      const double start_costdiff =
          nodes[start].cost - model.GetLiteralCosts(0, start);
      const int* dist_cache2 = &nodes[start].distance_cache[0];

      // Look for last distance matches using the distance cache from this
      // starting position.
      size_t best_len = min_len - 1;
      for (size_t j = 0; j < kNumDistanceShortCodes; ++j) {
        const size_t idx = kDistanceCacheIndex[j];
        const size_t backward =
            static_cast<size_t>(dist_cache2[idx] + kDistanceCacheOffset[j]);
        size_t prev_ix = cur_ix - backward;
        if (prev_ix >= cur_ix) {
          continue;
        }
        if (PREDICT_FALSE(backward > max_distance)) {
          continue;
        }
        prev_ix &= ringbuffer_mask;

        if (cur_ix_masked + best_len > ringbuffer_mask ||
            prev_ix + best_len > ringbuffer_mask ||
            ringbuffer[cur_ix_masked + best_len] !=
            ringbuffer[prev_ix + best_len]) {
          continue;
        }
        const size_t len =
            FindMatchLengthWithLimit(&ringbuffer[prev_ix],
                                     &ringbuffer[cur_ix_masked],
                                     max_length);
        for (size_t l = best_len + 1; l <= len; ++l) {
          const size_t inslen = i - start;
          double cmd_cost = model.GetCommandCost(j, l, inslen);
          double cost = start_costdiff + cmd_cost + model.GetLiteralCosts(0, i);
          if (cost < nodes[i + l].cost) {
            UpdateZopfliNode(&nodes[0], i, start, l, l, backward, j,
                             max_distance, dist_cache2, cost);
          }
          best_len = l;
        }
      }

      // At higher iterations look only for new last distance matches, since
      // looking only for new command start positions with the same distances
      // does not help much.
      if (k >= 2) continue;

      // Loop through all possible copy lengths at this position.
      size_t len = min_len;
      for (size_t j = 0; j < num_matches[i]; ++j) {
        BackwardMatch match = matches[cur_match_pos + j];
        size_t dist = match.distance;
        bool is_dictionary_match = dist > max_distance;
        // We already tried all possible last distance matches, so we can use
        // normal distance code here.
        size_t dist_code = dist + 15;
        // Try all copy lengths up until the maximum copy length corresponding
        // to this distance. If the distance refers to the static dictionary, or
        // the maximum length is long enough, try only one maximum length.
        size_t max_len = match.length();
        if (len < max_len && (is_dictionary_match || max_len > kMaxZopfliLen)) {
          len = max_len;
        }
        for (; len <= max_len; ++len) {
          size_t len_code = is_dictionary_match ? match.length_code() : len;
          const size_t inslen = i - start;
          double cmd_cost = model.GetCommandCost(dist_code, len_code, inslen);
          double cost = start_costdiff + cmd_cost + model.GetLiteralCosts(0, i);
          if (cost < nodes[i + len].cost) {
            UpdateZopfliNode(&nodes[0], i, start, len, len_code, dist,
                             dist_code, max_distance, dist_cache2, cost);
          }
        }
      }
    }

    cur_match_pos += num_matches[i];

    // The zopflification can be too slow in case of very long lengths, so in
    // such case skip it all, it does not cost a lot of compression ratio.
    if (num_matches[i] == 1 &&
        matches[cur_match_pos - 1].length() > kMaxZopfliLen) {
      i += matches[cur_match_pos - 1].length() - 1;
      queue.Clear();
    }
  }

  std::vector<uint32_t> backwards;
  size_t index = num_bytes;
  while (nodes[index].cost == kInfinity) --index;
  while (index != 0) {
    size_t len = nodes[index].length + nodes[index].insert_length;
    backwards.push_back(static_cast<uint32_t>(len));
    index -= len;
  }

  std::vector<uint32_t> path;
  for (size_t i = backwards.size(); i > 0; i--) {
    path.push_back(backwards[i - 1]);
  }

  size_t pos = 0;
  for (size_t i = 0; i < path.size(); i++) {
    const ZopfliNode& next = nodes[pos + path[i]];
    size_t copy_length = next.length;
    size_t insert_length = next.insert_length;
    pos += insert_length;
    if (i == 0) {
      insert_length += *last_insert_len;
      *last_insert_len = 0;
    }
    size_t distance = next.distance;
    size_t len_code = next.length_code;
    size_t max_distance = std::min(position + pos, max_backward_limit);
    bool is_dictionary = (distance > max_distance);
    size_t dist_code = next.distance_code;

    Command cmd(insert_length, copy_length, len_code, dist_code);
    *commands++ = cmd;

    if (!is_dictionary && dist_code > 0) {
      dist_cache[3] = dist_cache[2];
      dist_cache[2] = dist_cache[1];
      dist_cache[1] = dist_cache[0];
      dist_cache[0] = static_cast<int>(distance);
    }

    *num_literals += insert_length;
    insert_length = 0;
    pos += copy_length;
  }
  *last_insert_len += num_bytes - pos;
  *num_commands += static_cast<size_t>(commands - orig_commands);
}

template<typename Hasher>
void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              bool is_last,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const int quality,
                              const int lgwin,
                              Hasher* hasher,
                              int* dist_cache,
                              size_t* last_insert_len,
                              Command* commands,
                              size_t* num_commands,
                              size_t* num_literals) {
  // Set maximum distance, see section 9.1. of the spec.
  const size_t max_backward_limit = (1 << lgwin) - 16;

  // Choose which init method is faster.
  // memset is about 100 times faster than hasher->InitForData().
  const size_t kMaxBytesForPartialHashInit = Hasher::kHashMapSize >> 7;
  if (position == 0 && is_last && num_bytes <= kMaxBytesForPartialHashInit) {
    hasher->InitForData(ringbuffer, num_bytes);
  } else {
    hasher->Init();
  }
  if (num_bytes >= 3 && position >= 3) {
    // Prepare the hashes for three last bytes of the last write.
    // These could not be calculated before, since they require knowledge
    // of both the previous and the current block.
    hasher->Store(&ringbuffer[(position - 3) & ringbuffer_mask],
                  static_cast<uint32_t>(position - 3));
    hasher->Store(&ringbuffer[(position - 2) & ringbuffer_mask],
                  static_cast<uint32_t>(position - 2));
    hasher->Store(&ringbuffer[(position - 1) & ringbuffer_mask],
                  static_cast<uint32_t>(position - 1));
  }
  const Command * const orig_commands = commands;
  size_t insert_length = *last_insert_len;
  size_t i = position & ringbuffer_mask;
  const size_t i_diff = position - i;
  const size_t i_end = i + num_bytes;

  // For speed up heuristics for random data.
  const size_t random_heuristics_window_size = quality < 9 ? 64 : 512;
  size_t apply_random_heuristics = i + random_heuristics_window_size;

  // Minimum score to accept a backward reference.
  const int kMinScore = 4.0;

  while (i + Hasher::kHashTypeLength - 1 < i_end) {
    size_t max_length = i_end - i;
    size_t max_distance = std::min(i + i_diff, max_backward_limit);
    size_t best_len = 0;
    size_t best_len_code = 0;
    size_t best_dist = 0;
    double best_score = kMinScore;
    bool match_found = hasher->FindLongestMatch(
        ringbuffer, ringbuffer_mask,
        dist_cache, static_cast<uint32_t>(i + i_diff), max_length, max_distance,
        &best_len, &best_len_code, &best_dist, &best_score);
    if (match_found) {
      // Found a match. Let's look for something even better ahead.
      int delayed_backward_references_in_row = 0;
      for (;;) {
        --max_length;
        size_t best_len_2 =
            quality < 5 ? std::min(best_len - 1, max_length) : 0;
        size_t best_len_code_2 = 0;
        size_t best_dist_2 = 0;
        double best_score_2 = kMinScore;
        max_distance = std::min(i + i_diff + 1, max_backward_limit);
        match_found = hasher->FindLongestMatch(
            ringbuffer, ringbuffer_mask,
            dist_cache, static_cast<uint32_t>(i + i_diff + 1),
            max_length, max_distance,
            &best_len_2, &best_len_code_2, &best_dist_2, &best_score_2);
        double cost_diff_lazy = 7.0;
        if (match_found && best_score_2 >= best_score + cost_diff_lazy) {
          // Ok, let's just write one byte for now and start a match from the
            // next byte.
          ++i;
          ++insert_length;
          best_len = best_len_2;
          best_len_code = best_len_code_2;
          best_dist = best_dist_2;
          best_score = best_score_2;
          if (++delayed_backward_references_in_row < 4) {
            continue;
          }
        }
        break;
      }
      apply_random_heuristics =
          i + 2 * best_len + random_heuristics_window_size;
      max_distance = std::min(i + i_diff, max_backward_limit);
      // The first 16 codes are special shortcodes, and the minimum offset is 1.
      size_t distance_code =
          ComputeDistanceCode(best_dist, max_distance, quality, dist_cache);
      if (best_dist <= max_distance && distance_code > 0) {
        dist_cache[3] = dist_cache[2];
        dist_cache[2] = dist_cache[1];
        dist_cache[1] = dist_cache[0];
        dist_cache[0] = static_cast<int>(best_dist);
      }
      Command cmd(insert_length, best_len, best_len_code, distance_code);
      *commands++ = cmd;
      *num_literals += insert_length;
      insert_length = 0;
      // Put the hash keys into the table, if there are enough
      // bytes left.
      for (size_t j = 2; j < best_len; ++j) {
        hasher->Store(&ringbuffer[i + j],
                      static_cast<uint32_t>(i + i_diff + j));
      }
      i += best_len;
    } else {
      ++insert_length;
      ++i;
      // If we have not seen matches for a long time, we can skip some
      // match lookups. Unsuccessful match lookups are very very expensive
      // and this kind of a heuristic speeds up compression quite
      // a lot.
      if (i > apply_random_heuristics) {
        // Going through uncompressible data, jump.
        if (i > apply_random_heuristics + 4 * random_heuristics_window_size) {
          // It is quite a long time since we saw a copy, so we assume
          // that this data is not compressible, and store hashes less
          // often. Hashes of non compressible data are less likely to
          // turn out to be useful in the future, too, so we store less of
          // them to not to flood out the hash table of good compressible
          // data.
          size_t i_jump = std::min(i + 16, i_end - 4);
          for (; i < i_jump; i += 4) {
            hasher->Store(ringbuffer + i, static_cast<uint32_t>(i + i_diff));
            insert_length += 4;
          }
        } else {
          size_t i_jump = std::min(i + 8, i_end - 3);
          for (; i < i_jump; i += 2) {
            hasher->Store(ringbuffer + i, static_cast<uint32_t>(i + i_diff));
            insert_length += 2;
          }
        }
      }
    }
  }
  insert_length += i_end - i;
  *last_insert_len = insert_length;
  *num_commands += static_cast<size_t>(commands - orig_commands);
}

void CreateBackwardReferences(size_t num_bytes,
                              size_t position,
                              bool is_last,
                              const uint8_t* ringbuffer,
                              size_t ringbuffer_mask,
                              const int quality,
                              const int lgwin,
                              Hashers* hashers,
                              int hash_type,
                              int* dist_cache,
                              size_t* last_insert_len,
                              Command* commands,
                              size_t* num_commands,
                              size_t* num_literals) {
  bool zopflify = quality > 9;
  if (zopflify) {
    Hashers::H10* hasher = hashers->hash_h10;
    hasher->Init(lgwin, position, num_bytes, is_last);
    if (num_bytes >= 3 && position >= kMaxTreeCompLength) {
      // Store the last `kMaxTreeCompLength - 1` positions in the hasher.
      // These could not be calculated before, since they require knowledge
      // of both the previous and the current block.
      for (size_t i = position - kMaxTreeCompLength + 1; i < position; ++i) {
        hasher->Store(ringbuffer, ringbuffer_mask, i, num_bytes + position - i);
      }
    }
    // Set maximum distance, see section 9.1. of the spec.
    const size_t max_backward_limit = (1 << lgwin) - 16;
    std::vector<uint32_t> num_matches(num_bytes);
    std::vector<BackwardMatch> matches(4 * num_bytes);
    size_t cur_match_pos = 0;
    for (size_t i = 0; i + 3 < num_bytes; ++i) {
      size_t max_distance = std::min(position + i, max_backward_limit);
      size_t max_length = num_bytes - i;
      // Ensure that we have enough free slots.
      if (matches.size() < cur_match_pos + Hashers::H10::kMaxNumMatches) {
        matches.resize(cur_match_pos + Hashers::H10::kMaxNumMatches);
      }
      size_t num_found_matches = hasher->FindAllMatches(
          ringbuffer, ringbuffer_mask, position + i, max_length, max_distance,
          &matches[cur_match_pos]);
      const size_t cur_match_end = cur_match_pos + num_found_matches;
      for (size_t j = cur_match_pos; j + 1 < cur_match_end; ++j) {
        assert(matches[j].length() < matches[j + 1].length());
        assert(matches[j].distance > max_distance ||
               matches[j].distance <= matches[j + 1].distance);
      }
      num_matches[i] = static_cast<uint32_t>(num_found_matches);
      if (num_found_matches > 0) {
        const size_t match_len = matches[cur_match_end - 1].length();
        if (match_len > kMaxZopfliLen) {
          matches[cur_match_pos++] = matches[cur_match_end - 1];
          num_matches[i] = 1;
          for (size_t j = 1; j < match_len; ++j) {
            ++i;
            if (match_len - j < 64) {
              hasher->Store(ringbuffer, ringbuffer_mask, position + i,
                            num_bytes - i);
            }
            num_matches[i] = 0;
          }
        } else {
          cur_match_pos = cur_match_end;
        }
      }
    }
    size_t orig_num_literals = *num_literals;
    size_t orig_last_insert_len = *last_insert_len;
    int orig_dist_cache[4] = {
      dist_cache[0], dist_cache[1], dist_cache[2], dist_cache[3]
    };
    size_t orig_num_commands = *num_commands;
    static const size_t kIterations = 2;
    for (size_t i = 0; i < kIterations; i++) {
      ZopfliCostModel model;
      if (i == 0) {
        model.SetFromLiteralCosts(num_bytes, position,
                                  ringbuffer, ringbuffer_mask);
      } else {
        model.SetFromCommands(num_bytes, position,
                              ringbuffer, ringbuffer_mask,
                              commands, *num_commands - orig_num_commands,
                              orig_last_insert_len);
      }
      *num_commands = orig_num_commands;
      *num_literals = orig_num_literals;
      *last_insert_len = orig_last_insert_len;
      memcpy(dist_cache, orig_dist_cache, 4 * sizeof(dist_cache[0]));
      ZopfliIterate(num_bytes, position, ringbuffer, ringbuffer_mask,
                    max_backward_limit, model, num_matches, matches, dist_cache,
                    last_insert_len, commands, num_commands, num_literals);
    }
    return;
  }

  switch (hash_type) {
    case 2:
      CreateBackwardReferences<Hashers::H2>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h2, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 3:
      CreateBackwardReferences<Hashers::H3>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h3, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 4:
      CreateBackwardReferences<Hashers::H4>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h4, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 5:
      CreateBackwardReferences<Hashers::H5>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h5, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 6:
      CreateBackwardReferences<Hashers::H6>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h6, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 7:
      CreateBackwardReferences<Hashers::H7>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h7, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 8:
      CreateBackwardReferences<Hashers::H8>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h8, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    case 9:
      CreateBackwardReferences<Hashers::H9>(
          num_bytes, position, is_last, ringbuffer, ringbuffer_mask,
          quality, lgwin, hashers->hash_h9, dist_cache,
          last_insert_len, commands, num_commands, num_literals);
      break;
    default:
      break;
  }
}

}  // namespace brotli

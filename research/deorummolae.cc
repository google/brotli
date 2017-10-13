#include "./deorummolae.h"

#include <array>
#include <cstdio>

#include "./esaxx/sais.hxx"

/* Used for quick SA-entry to file mapping. Each file is padded to size that
   is a multiple of chunk size. */
#define CHUNK_SIZE 64
/* Length of substring that is considered to be covered by dictionary string. */
#define CUT_MATCH 6
/* Minimal dictionary entry size. */
#define MIN_MATCH 24

/* Non tunable definitions. */
#define CHUNK_MASK (CHUNK_SIZE - 1)
#define COVERAGE_SIZE (1 << (LOG_MAX_FILES - 6))

/* File coverage: every bit set to 1 denotes a file covered by an isle. */
typedef std::array<uint64_t, COVERAGE_SIZE> Coverage;

static int popcount(uint64_t u) { return __builtin_popcountll(u); }

/* Condense terminators and pad file entries. */
static void rewriteText(std::vector<int>* text) {
  int terminator = text->back();
  int prev = terminator;
  size_t to = 0;
  for (size_t from = 0; from < text->size(); ++from) {
    int next = text->at(from);
    if (next < 256 || prev < 256) {
      text->at(to++) = next;
      if (next >= 256) terminator = next;
    }
    prev = next;
  }
  text->resize(to);
  if (text->empty()) text->push_back(terminator);
  while (text->size() & CHUNK_MASK) text->push_back(terminator);
}

/* Reenumerate terminators for smaller alphabet. */
static void remapTerminators(std::vector<int>* text, int* next_terminator) {
  int prev = -1;
  int x = 256;
  for (size_t i = 0; i < text->size(); ++i) {
    int next = text->at(i);
    if (next < 256) {  // Char.
      // Do nothing.
    } else if (prev < 256) {  // Terminator after char.
      next = x++;
    } else {  // Terminator after terminator.
      next = prev;
    }
    text->at(i) = next;
    prev = next;
  }
  *next_terminator = x;
}

/* Combine all file entries; create mapping position->file. */
static void buildFullText(std::vector<std::vector<int>>* data,
    std::vector<int>* full_text, std::vector<size_t>* file_map,
    std::vector<size_t>* file_offset, int* next_terminator) {
  file_map->resize(0);
  file_offset->resize(0);
  full_text->resize(0);
  for (size_t i = 0; i < data->size(); ++i) {
    file_offset->push_back(full_text->size());
    std::vector<int>& file = data->at(i);
    rewriteText(&file);
    full_text->insert(full_text->end(), file.begin(), file.end());
    file_map->insert(file_map->end(), file.size() / CHUNK_SIZE, i);
  }
  if (false) remapTerminators(full_text, next_terminator);
}

/* Build longest-common-prefix based on suffix array and text.
   TODO: borrowed -> unknown efficiency. */
static void buildLcp(std::vector<int>* text, std::vector<int>* sa,
    std::vector<int>* lcp, std::vector<int>* invese_sa) {
  int size = static_cast<int>(text->size());
  lcp->resize(size);
  int k = 0;
  lcp->at(size - 1) = 0;
  for (int i = 0; i < size; ++i) {
    if (invese_sa->at(i) == size - 1) {
      k = 0;
      continue;
    }
    int j = sa->at(invese_sa->at(i) + 1);  // Suffix which follow i-th suffix.
    while (i + k < size && j + k < size && text->at(i + k) == text->at(j + k)) {
      ++k;
    }
    lcp->at(invese_sa->at(i)) = k;
    if (k > 0) --k;
  }
}

/* Isle is a range in SA with LCP not less than some value.
   When we raise the LCP requirement, the isle sunks and smaller isles appear
   instead. */
typedef struct {
  int lcp;
  int l;
  int r;
  Coverage coverage;
} Isle;

/* Helper routine for `cutMatch`. */
static void poisonData(int pos, int length, std::vector<std::vector<int>>* data,
    std::vector<size_t>* file_map, std::vector<size_t>* file_offset,
    int* next_terminator) {
  size_t f = file_map->at(pos / CHUNK_SIZE);
  pos -= file_offset->at(f);
  std::vector<int>& file = data->at(f);
  int l = (length == CUT_MATCH) ? CUT_MATCH : 1;
  for (int j = 0; j < l; j++, pos++) {
    if (file[pos] >= 256) continue;
    if (file[pos + 1] >= 256) {
      file[pos] = file[pos + 1];
    } else if (pos > 0 && file[pos - 1] >= 256) {
      file[pos] = file[pos - 1];
    } else {
      file[pos] = (*next_terminator)++;
    }
  }
}

/* Remove substrings of a given match from files.
   Substrings are replaced with unique terminators, so next iteration SA would
   not allow to cross removed areas. */
static void cutMatch(std::vector<std::vector<int>>* data, int index, int length,
    std::vector<int>* sa, std::vector<int>* lcp, std::vector<int>* invese_sa,
    int* next_terminator, std::vector<size_t>* file_map,
    std::vector<size_t>* file_offset) {
  while (length >= CUT_MATCH) {
    int i = index;
    while (lcp->at(i) >= length) {
      i++;
      poisonData(
          sa->at(i), length, data, file_map, file_offset, next_terminator);
    }
    while (true) {
      poisonData(
          sa->at(index), length, data, file_map, file_offset, next_terminator);
      if (index == 0 || lcp->at(index - 1) < length) break;
      index--;
    }
    length--;
    index = invese_sa->at(sa->at(index) + 1);
  }
}

std::string DM_generate(size_t dictionary_size_limit,
    const std::vector<size_t>& sample_sizes, const uint8_t* sample_data) {
  {
    uint64_t tmp = 0;
    if (popcount(tmp - 1u) != 64) {
      fprintf(stderr, "64-bit platform is required\n");
      return 0;
    }
  }

  /* Could use 256 + '0' for easier debugging. */
  int next_terminator = 256;

  std::string output;
  std::vector<std::vector<int>> data;

  size_t offset = 0;
  size_t num_samples = sample_sizes.size();
  if (num_samples > MAX_FILES) num_samples = MAX_FILES;
  for (size_t n = 0; n < num_samples; ++n) {
    size_t next_offset = offset + sample_sizes[n];
    data.push_back(
        std::vector<int>(sample_data + offset, sample_data + next_offset));
    offset = next_offset;
    data.back().push_back(next_terminator++);
  }

  /* Most arrays are allocated once, and then just resized to smaller and
     smaller sizes. */
  std::vector<int> full_text;
  std::vector<size_t> file_map;
  std::vector<size_t> file_offset;
  std::vector<int> sa;
  std::vector<int> invese_sa;
  std::vector<int> lcp;
  std::vector<Isle> isles;
  std::vector<char> output_data;
  size_t total = 0;
  size_t total_cost = 0;
  size_t best_cost;
  Isle best_isle;
  int min_count = num_samples;

  while (true) {
    size_t max_match = dictionary_size_limit - total;
    buildFullText(&data, &full_text, &file_map, &file_offset, &next_terminator);
    sa.resize(full_text.size());
    saisxx(full_text.data(), sa.data(), static_cast<int>(full_text.size()),
        next_terminator);
    invese_sa.resize(full_text.size());
    for (int i = 0; i < full_text.size(); ++i) invese_sa[sa[i]] = i;
    buildLcp(&full_text, &sa, &lcp, &invese_sa);

    /* Do not rebuild SA/LCP, just use different selection. */
  retry:
    best_cost = 0;
    best_isle = {0, 0, 0, {{0}}};
    isles.resize(0);
    isles.push_back(best_isle);

    for (int i = 0; i < static_cast<int>(lcp.size()); ++i) {
      int l = i;
      Coverage cov = {{0}};
      int f = file_map[sa[i] / CHUNK_SIZE];
      cov[f >> 6] = ((uint64_t)1) << (f & 63);
      while (lcp[i] < isles.back().lcp) {
        Isle& top = isles.back();
        top.r = i;
        l = top.l;
        for (size_t x = 0; x < cov.size(); ++x) cov[x] |= top.coverage[x];
        int count = 0;
        for (size_t x = 0; x < cov.size(); ++x) count += popcount(cov[x]);
        int effective_lcp = top.lcp;
        /* Restrict (last) dictionary entry length. */
        if (effective_lcp > max_match) effective_lcp = max_match;
        int cost = count * effective_lcp;
        if (cost > best_cost && count >= min_count &&
            effective_lcp >= MIN_MATCH) {
          best_cost = cost;
          best_isle = top;
          best_isle.lcp = effective_lcp;
        }
        isles.pop_back();
        for (size_t x = 0; x < cov.size(); ++x) {
          isles.back().coverage[x] |= cov[x];
        }
      }
      if (lcp[i] > isles.back().lcp) isles.push_back({lcp[i], l, 0, {{0}}});
      for (size_t x = 0; x < cov.size(); ++x) {
        isles.back().coverage[x] |= cov[x];
      }
    }

    /* When saturated matches do not match length restrictions, lower the
       saturation requirements. */
    if (best_cost == 0 || best_isle.lcp < MIN_MATCH) {
      if (min_count >= 8) {
        min_count = (min_count * 7) / 8;
        fprintf(stderr, "Retry: min_count=%d\n", min_count);
        goto retry;
      }
      break;
    }

    /* Save the entry. */
    fprintf(stderr, "Savings: %zu+%zu, dictionary: %zu+%d\n",
        total_cost, best_cost, total, best_isle.lcp);
    int* piece = &full_text[sa[best_isle.l]];
    output.insert(output.end(), piece, piece + best_isle.lcp);
    total += best_isle.lcp;
    total_cost += best_cost;
    cutMatch(&data, best_isle.l, best_isle.lcp, &sa, &lcp, &invese_sa,
        &next_terminator, &file_map, &file_offset);
    if (total >= dictionary_size_limit) break;
  }

  return output;
}

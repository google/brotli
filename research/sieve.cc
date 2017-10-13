#include "./sieve.h"

typedef struct Slot {
  uint32_t next;
  uint32_t offset;
  uint16_t presence;
  uint16_t mark;
} Slot;

static size_t dryRun(size_t sliceLen, Slot* map, uint32_t* shortcut, size_t end,
    size_t middle, uint16_t minPresence, uint16_t iteration) {
  int from = -2;
  int to = -1;
  size_t result = 0;
  uint16_t targetPresence = minPresence;
  for (uint32_t i = 0; i < end; ++i) {
    if (i == middle) {
      targetPresence++;
    }
    Slot& item = map[shortcut[i]];
    if (item.mark != iteration) {
      item.mark = iteration;
      if (item.presence >= targetPresence) {
        if (to < i) {
          if (from > 0) {
            result += to - from;
          }
          from = i;
        }
        to = i + sliceLen;
      }
    }
  }
  if (from > 0) {
    result += to - from;
  }
  return result;
}

static std::string createDictionary(const uint8_t* data, size_t sliceLen,
    Slot* map, uint32_t* shortcut, size_t end, size_t middle,
    uint16_t minPresence, uint16_t iteration) {
  std::string output;
  int from = -2;
  int to = -1;
  uint16_t targetPresence = minPresence;
  for (uint32_t i = 0; i < end; ++i) {
    if (i == middle) {
      targetPresence++;
    }
    Slot& item = map[shortcut[i]];
    if (item.mark != iteration) {
      item.mark = iteration;
      if (item.presence >= targetPresence) {
        if (to < i) {
          if (from > 0) {
            output.insert(output.end(), &data[from], &data[to]);
          }
          from = i;
        }
        to = i + sliceLen;
      }
    }
  }
  if (from > 0) {
    output.insert(output.end(), &data[from], &data[to]);
  }
  return output;
}

std::string sieve_generate(size_t dictionary_size_limit, size_t slice_len,
    const std::vector<size_t>& sample_sizes, const uint8_t* sample_data) {
  /* Parameters aliasing */
  size_t targetSize = dictionary_size_limit;
  size_t sliceLen = slice_len;
  const uint8_t* data = sample_data;

  size_t total = 0;
  std::vector<size_t> offsets;
  for (size_t i = 0; i < sample_sizes.size(); ++i) {
    total += sample_sizes[i];
    offsets.push_back(total);
  }

  /*****************************************************************************
   * Build coverage map.
   ****************************************************************************/
  std::vector<Slot> map;
  std::vector<uint32_t> shortcut;
  map.push_back({0, 0, 0, 0});
  size_t end = total - sliceLen;
  int hashLen = 8;
  while ((1 << hashLen) < end) {
    hashLen += 3;
  }
  hashLen -= 3;
  uint32_t hashMask = (1u << hashLen) - 1u;
  std::vector<uint32_t> hashHead(1 << hashLen);
  uint32_t hashSlot = 1;
  uint16_t piece = 0;
  uint32_t hash = 0;
  int lShift = 3;
  int rShift = hashLen - lShift;
  for (int i = 0; i < sliceLen - 1; ++i) {
    uint32_t v = data[i];
    hash = (((hash << lShift) | (hash >> rShift)) & hashMask) ^ v;
  }
  int lShiftX = (lShift * (sliceLen - 1)) % hashLen;
  int rShiftX = hashLen - lShiftX;
  for (uint32_t i = 0; i < end; ++i) {
    uint32_t v = data[i + sliceLen - 1];
    hash = (((hash << lShift) | (hash >> rShift)) & hashMask) ^ v;

    if (offsets[piece] == i) {
      piece++;
    }
    uint32_t slot = hashHead[hash];
    while (slot != 0) {
      Slot& item = map[slot];
      int start = item.offset;
      bool miss = false;
      for (size_t j = 0; j < sliceLen; ++j) {
        if (data[i + j] != data[start + j]) {
          miss = true;
          break;
        }
      }
      if (!miss) {
        if (item.mark != piece) {
          item.mark = piece;
          item.presence++;
        }
        shortcut.push_back(slot);
        break;
      }
      slot = item.next;
    }
    if (slot == 0) {
      map.push_back({hashHead[hash], i, 1, piece});
      hashHead[hash] = hashSlot;
      shortcut.push_back(hashSlot);
      hashSlot++;
    }
    v = data[i];
    hash ^= ((v << lShiftX) | (v >> rShiftX)) & hashMask;
  }

  /*****************************************************************************
   * Build dictionary of specified size.
   ****************************************************************************/
  size_t a = 1;
  size_t size = dryRun(
      sliceLen, map.data(), shortcut.data(), end, end, a, ++piece);
  /* Maximal output is smaller than target. */
  if (size <= targetSize) {
    return createDictionary(
        data, sliceLen, map.data(), shortcut.data(), end, end, a, ++piece);
  }

  size_t b = offsets.size();
  size = dryRun(sliceLen, map.data(), shortcut.data(), end, end, b, ++piece);
  if (size == targetSize) {
    return createDictionary(
        data, sliceLen, map.data(), shortcut.data(), end, end, b, ++piece);
  }
  /* Run binary search. */
  if (size < targetSize) {
    /* size(a) > targetSize > size(b) && a < m < b */
    while (a + 1 < b) {
      size_t m = (a + b) / 2;
      size = dryRun(
          sliceLen, map.data(), shortcut.data(), end, end, m, ++piece);
      if (size < targetSize) {
        b = m;
      } else if (size > targetSize) {
        a = m;
      } else {
        return createDictionary(
            data, sliceLen, map.data(), shortcut.data(), end, end, b, ++piece);
      }
    }
  } else {
    a = b;
  }
  /* size(minPresence) > targetSize > size(minPresence + 1) */
  size_t minPresence = a;
  a = 0;
  b = end;
  /* size(a) < targetSize < size(b) && a < m < b */
  while (a + 1 < b) {
    size_t m = (a + b) / 2;
    size = dryRun(
        sliceLen, map.data(), shortcut.data(), end, m, minPresence, ++piece);
    if (size < targetSize) {
      a = m;
    } else if (size > targetSize) {
      b = m;
    } else {
      return createDictionary(data, sliceLen, map.data(), shortcut.data(), end,
          m, minPresence, ++piece);
    }
  }

  bool unrestricted = false;
  if (minPresence <= 2 && !unrestricted) {
    minPresence = 2;
    a = end;
  }
  return createDictionary(data, sliceLen, map.data(), shortcut.data(), end, a,
      minPresence, ++piece);
}

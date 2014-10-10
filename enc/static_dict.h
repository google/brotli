// Copyright 2013 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Class to model the static dictionary.

#ifndef BROTLI_ENC_STATIC_DICT_H_
#define BROTLI_ENC_STATIC_DICT_H_

#include <algorithm>
#include <unordered_map>
#include <string>

namespace brotli {

class StaticDictionary {
 public:
  StaticDictionary() {}
  void Insert(const std::string &str, int len, int dist) {
    int ix = (dist << 6) + len;
    std::unordered_map<std::string, int>::const_iterator it = map_.find(str);
    if (it != map_.end() && ix >= it->second) {
      return;
    }
    map_[str] = ix;
    int v = 0;
    for (int i = 0; i < 4 && i < str.size(); ++i) {
      v += str[i] << (8 * i);
    }
    if (prefix_map_[v] < str.size()) {
      prefix_map_[v] = str.size();
    }
  }
  int GetLength(int v) const {
    std::unordered_map<int, int>::const_iterator it = prefix_map_.find(v);
    if (it == prefix_map_.end()) {
      return 0;
    }
    return it->second;
  }
  bool Get(const std::string &str, int *len, int *dist) const {
    std::unordered_map<std::string, int>::const_iterator it = map_.find(str);
    if (it == map_.end()) {
      return false;
    }
    int v = it->second;
    *len = v & 63;
    *dist = v >> 6;
    return true;
  }
 private:
  std::unordered_map<std::string, int> map_;
  std::unordered_map<int, int> prefix_map_;
};

}  // namespace brotli

#endif  // BROTLI_ENC_STATIC_DICT_H_

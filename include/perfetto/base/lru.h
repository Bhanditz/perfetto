/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef INCLUDE_PERFETTO_BASE_LRU_H_
#define INCLUDE_PERFETTO_BASE_LRU_H_

#include <list>
#include <map>

namespace perfetto {
namespace base {

namespace {
template <typename T>
class PtrLess {
 public:
  bool operator()(T* lhs, T* rhs) const { return lhs != rhs && *lhs < *rhs; }
};
}  // namespace

/*
 * LRUCache is a memory efficient LRU map.
 *
 * It does not store the value of the key twice but rather
 * stores a pointer in the key of the map.
 */
template <typename K, typename V>
class LRUCache {
 public:
  explicit LRUCache(size_t capacity) : capacity_(capacity) {}

  const V* get(const K& k) {
    const auto& it = item_map_.find(&k);
    if (it == item_map_.end()) {
      return nullptr;
    }
    auto list_entry = it->second;
    // Bump this item to the front of the cache.
    // We can borrow the existing item's key and value because insert
    // does not care about it.
    insert(it, std::move(list_entry->first), std::move(list_entry->second));
    return &item_list_.cbegin()->second;
  }

  void insert(const K k, const V v) {
    return insert(item_map_.find(&k), std::move(k), std::move(v));
  }

 private:
  using list_item_type_ = std::pair<const K, const V>;
  using list_iterator_type_ = typename std::list<list_item_type_>::iterator;
  using map_type_ = std::map<const K*, list_iterator_type_, PtrLess<const K>>;

  void insert(typename map_type_::iterator existing, const K k, const V v) {
    if (existing != item_map_.end()) {
      auto snd = existing->second;
      // The entry in the map contains a pointer to the element in
      // item_list_, so delete that first.
      item_map_.erase(existing);
      item_list_.erase(snd);
    }

    if (item_map_.size() == capacity_) {
      auto last_elem = item_list_.end();
      last_elem--;
      item_map_.erase(&(last_elem->first));
      item_list_.erase(last_elem);
    }
    item_list_.emplace_front(std::move(k), std::move(v));
    item_map_.emplace(&(item_list_.begin()->first), item_list_.begin());
  }

  size_t capacity_;
  map_type_ item_map_;
  std::list<list_item_type_> item_list_;
};

}  // namespace base
}  // namespace perfetto

#endif  // INCLUDE_PERFETTO_BASE_LRU_H_

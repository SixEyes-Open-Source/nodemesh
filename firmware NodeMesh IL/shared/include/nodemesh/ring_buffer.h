#pragma once

#include <stddef.h>

namespace nodemesh {

template <typename T, size_t Capacity>
class RingBuffer {
public:
  bool push(const T &item) {
    if (count_ == Capacity) {
      return false;
    }
    data_[head_] = item;
    head_ = (head_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(T &out) {
    if (count_ == 0) {
      return false;
    }
    out = data_[tail_];
    tail_ = (tail_ + 1) % Capacity;
    --count_;
    return true;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == Capacity; }

private:
  T data_[Capacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
};

} // namespace nodemesh

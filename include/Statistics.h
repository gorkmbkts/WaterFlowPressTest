#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

template <typename T>
class RunningMeanVariance {
 public:
  void reset() {
    count_ = 0;
    mean_ = 0;
    m2_ = 0;
  }

  void push(T value) {
    ++count_;
    T delta = value - mean_;
    mean_ += delta / static_cast<T>(count_);
    T delta2 = value - mean_;
    m2_ += delta * delta2;
  }

  size_t count() const { return count_; }
  T mean() const { return mean_; }
  T variance() const { return (count_ > 1) ? m2_ / static_cast<T>(count_ - 1) : 0; }
  T stddev() const { return std::sqrt(variance()); }

 private:
  size_t count_ = 0;
  T mean_ = 0;
  T m2_ = 0;
};

class RunningMinMax {
 public:
  void reset() {
    min_ = std::numeric_limits<float>::max();
    max_ = std::numeric_limits<float>::lowest();
  }

  void push(float value) {
    if (value < min_) min_ = value;
    if (value > max_) max_ = value;
  }

  float min() const { return min_; }
  float max() const { return max_; }

 private:
  float min_ = std::numeric_limits<float>::max();
  float max_ = std::numeric_limits<float>::lowest();
};

// Fixed-size ring buffer percentile calculator
// Computes percentile on demand using nth_element to avoid full sorting
// Template parameter N is compile-time capacity.
template <size_t N>
class RollingPercentiles {
 public:
  void push(float value) {
    buffer_[index_] = value;
    index_ = (index_ + 1) % N;
    if (count_ < N) {
      ++count_;
    } else {
      full_ = true;
    }
  }

  size_t size() const { return count_; }

    float percentile(float p) const {
      if (count_ == 0) return 0.0f;
      std::array<float, N> temp = buffer_;
      size_t valid = count_;
      if (!full_) {
        valid = index_;
      }
      float clipped = p;
      if (clipped < 0.0f) clipped = 0.0f;
      if (clipped > 100.0f) clipped = 100.0f;
      size_t rank = static_cast<size_t>((clipped / 100.0f) * (valid - 1));
      std::nth_element(temp.begin(), temp.begin() + rank, temp.begin() + valid);
      return temp[rank];
    }

  float median() const { return percentile(50.0f); }

 private:
  std::array<float, N> buffer_{};
  size_t index_ = 0;
  size_t count_ = 0;
  bool full_ = false;
};

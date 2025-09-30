#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

// Simple running statistics using Welford's algorithm and fixed-size history for percentiles.

template <size_t WindowSize>
class RunningStatistics {
 public:
  RunningStatistics() { reset(); }

  void reset() {
    count_ = 0;
    mean_ = 0.0f;
    m2_ = 0.0f;
    min_ = std::numeric_limits<float>::infinity();
    max_ = -std::numeric_limits<float>::infinity();
    head_ = 0;
    filled_ = false;
  }

  void push(float value) {
    if (!std::isfinite(value)) {
      return;
    }

    // Update Welford running stats
    count_++;
    float delta = value - mean_;
    mean_ += delta / static_cast<float>(count_);
    m2_ += delta * (value - mean_);
    min_ = std::min(min_, value);
    max_ = std::max(max_, value);

    history_[head_] = value;
    head_ = (head_ + 1) % WindowSize;
    if (head_ == 0) {
      filled_ = true;
    }
  }

  size_t count() const { return count_; }
  float mean() const { return mean_; }
  float variance() const { return (count_ > 1) ? m2_ / static_cast<float>(count_ - 1) : 0.0f; }
  float stddev() const { return std::sqrt(variance()); }
  float minimum() const { return (count_ == 0) ? 0.0f : min_; }
  float maximum() const { return (count_ == 0) ? 0.0f : max_; }

  float median() const { return percentile(0.5f); }

  float percentile(float pct) const {
    if (!filled_ && head_ == 0 && count_ == 0) {
      return 0.0f;
    }
    size_t valid = filled_ ? WindowSize : head_;
    if (valid == 0) {
      return 0.0f;
    }

    std::array<float, WindowSize> temp;
    std::copy(history_.begin(), history_.begin() + valid, temp.begin());
    std::sort(temp.begin(), temp.begin() + valid);

    float position = pct * static_cast<float>(valid - 1);
    size_t lower = static_cast<size_t>(std::floor(position));
    size_t upper = static_cast<size_t>(std::ceil(position));
    float fraction = position - static_cast<float>(lower);
    float lowerValue = temp[lower];
    float upperValue = temp[upper];
    return lowerValue + (upperValue - lowerValue) * fraction;
  }

 private:
  size_t count_ = 0;
  float mean_ = 0.0f;
  float m2_ = 0.0f;
  float min_ = 0.0f;
  float max_ = 0.0f;
  size_t head_ = 0;
  bool filled_ = false;
  std::array<float, WindowSize> history_{};
};


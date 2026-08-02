#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <vector>

namespace matchengine {

// Collects per-operation latency samples (in nanoseconds) and reports the
// distribution. Kept separate from the matching logic so timing code
// never runs on the hot path itself beyond two chrono calls per order.
class LatencyStats {
public:
    // Pre-allocates storage for the expected number of samples. Without
    // this, std::vector's own reallocation-and-copy on growth shows up as
    // a spurious multi-millisecond spike in the very latency numbers this
    // class exists to measure -- the measurement tool would be corrupting
    // its own measurement.
    void reserve(size_t expected_samples) { samples_.reserve(expected_samples); }

    void record(int64_t nanos) { samples_.push_back(nanos); }

    size_t count() const { return samples_.size(); }

    double min_ns() const {
        return samples_.empty() ? 0.0 : static_cast<double>(*std::min_element(samples_.begin(), samples_.end()));
    }

    double max_ns() const {
        return samples_.empty() ? 0.0 : static_cast<double>(*std::max_element(samples_.begin(), samples_.end()));
    }

    double avg_ns() const {
        if (samples_.empty()) return 0.0;
        int64_t sum = std::accumulate(samples_.begin(), samples_.end(), int64_t{0});
        return static_cast<double>(sum) / static_cast<double>(samples_.size());
    }

    // Percentile via nearest-rank on a sorted copy. p in [0, 100].
    double percentile_ns(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<int64_t> sorted(samples_.begin(), samples_.end());
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>((p / 100.0) * static_cast<double>(sorted.size() - 1));
        return static_cast<double>(sorted[idx]);
    }

private:
    std::vector<int64_t> samples_;
};

} // namespace matchengine

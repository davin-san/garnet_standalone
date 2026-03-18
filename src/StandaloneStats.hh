// StandaloneStats.hh — Shared latency histogram for standalone Garnet.
// Used by PACE, Uniform, and Ablation injection modes.
//
// Bucket layout MUST match gem5 PaceProfiler exactly:
//   fine[i]:   lat in [i, i+1) cycles,       i = 0..99
//   coarse[j]: lat in [100+j*10, ...) cycles, j = 0..89
//   ultra[k]:  lat in [1000+k*100,...) cycles, k = 0..89
//   overflow:  lat >= 10000 cycles
//
// Percentile algorithm: walk buckets in order; first bucket where cumulative
// count >= target is the answer.  Midpoints for coarse/ultra buckets.

#ifndef __STANDALONE_STATS_HH__
#define __STANDALONE_STATS_HH__

#include <array>
#include <cstdint>

namespace garnet {

struct LatHist {
    std::array<uint64_t, 100> fine{};    // [0,   100), width 1
    std::array<uint64_t, 90>  coarse{};  // [100, 1000), width 10
    std::array<uint64_t, 90>  ultra{};   // [1000,10000), width 100
    uint64_t overflow = 0;               // >= 10000

    void insert(uint64_t lat) {
        if      (lat < 100)   fine[lat]++;
        else if (lat < 1000)  coarse[(lat - 100) / 10]++;
        else if (lat < 10000) ultra[(lat - 1000) / 100]++;
        else                  overflow++;
    }

    void merge(const LatHist& o) {
        for (int i = 0; i < 100; ++i) fine[i]   += o.fine[i];
        for (int i = 0; i < 90;  ++i) coarse[i] += o.coarse[i];
        for (int i = 0; i < 90;  ++i) ultra[i]  += o.ultra[i];
        overflow += o.overflow;
    }

    uint64_t total() const {
        uint64_t t = overflow;
        for (auto v : fine)   t += v;
        for (auto v : coarse) t += v;
        for (auto v : ultra)  t += v;
        return t;
    }

    // pct in [0,1]: e.g. 0.99 for p99.
    double percentile(double pct) const {
        uint64_t tot = total();
        if (tot == 0) return 0.0;
        uint64_t target = (uint64_t)(pct * tot + 0.5);
        if (target == 0) target = 1;
        uint64_t cum = 0;
        for (int i = 0; i < 100; ++i) {
            cum += fine[i];
            if (cum >= target) return (double)i;
        }
        for (int i = 0; i < 90; ++i) {
            cum += coarse[i];
            if (cum >= target) return 100.0 + i * 10.0;
        }
        for (int i = 0; i < 90; ++i) {
            cum += ultra[i];
            if (cum >= target) return 1000.0 + i * 100.0;
        }
        return 10000.0;
    }

    double max_latency() const {
        if (overflow > 0) return 10000.0;
        for (int i = 89; i >= 0; --i)
            if (ultra[i] > 0) return 1000.0 + i * 100.0;
        for (int i = 89; i >= 0; --i)
            if (coarse[i] > 0) return 100.0 + i * 10.0;
        for (int i = 99; i >= 0; --i)
            if (fine[i] > 0) return (double)i;
        return 0.0;
    }
};

} // namespace garnet

#endif // __STANDALONE_STATS_HH__

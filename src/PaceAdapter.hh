// PACE Adapter — global coordinator for workload-driven injection.
// Owns all PaceTrafficGenerators, manages phase cycling, and collects metrics.

#ifndef __PACE_ADAPTER_HH__
#define __PACE_ADAPTER_HH__

#include <string>
#include <vector>
#include <map>
#include <cstdint>

#include "PaceProfile.hh"
#include "PaceTrafficGenerator.hh"
#include "NetworkInterface.hh"
#include "NetworkLink.hh"

namespace garnet {

// Ablation configuration: each flag disables one PACE feature.
// no_X = false (default) means the feature is ENABLED (normal PACE behavior).
// no_X = true means the feature is DISABLED (ablated / simplified).
struct PaceAblationConfig {
    bool no_per_source    = false;
    bool no_phases        = false;
    bool no_mshr          = false;
    bool no_remap         = false;
    bool no_weighted_dest = false;
    bool no_corr_response = false;
};

class PaceAdapter {
public:
    // Expose ablation config type as a nested alias for ergonomic use in main.cc.
    typedef PaceAblationConfig AblationConfig;

    // Per-phase aggregate metrics accumulated during simulation.
    struct PhaseMetrics {
        uint64_t packets_received = 0;
        uint64_t total_latency    = 0;
        uint64_t flits_received   = 0;
    };

    // Construct from a profile JSON path.
    // mshr_limit : max outstanding requests per core node (default 16)
    // seed       : RNG seed (added to per-node offset for reproducibility)
    PaceAdapter(const std::string& profile_path, int mshr_limit = 16, int seed = 42,
                const AblationConfig& ablation = AblationConfig());
    ~PaceAdapter();

    // Create a PaceTrafficGenerator for every NI in the topology and
    // attach it to the NI (replacing its existing SimpleTrafficGenerator).
    // Must be called after topo->build() and before the simulation loop.
    void init(const std::vector<NetworkInterface*>& nis, GarnetNetwork* net);

    // Advance phase counter.  Call once per simulation cycle, before NI wakeups.
    // Returns false when all phases are exhausted (simulation should stop).
    bool tick(uint64_t current_cycle);

    // True once all phases are done.
    bool is_done() const { return m_done; }

    // Sum of network_cycles across all phases — the total sim length.
    uint64_t total_network_cycles() const;

    // ---- Accessors for TGs (per-cycle injection decisions) ----

    int current_phase_idx() const { return m_current_phase; }
    int num_cpus()          const { return m_profile.num_cpus; }
    int num_dirs()          const { return m_profile.num_dirs; }
    int mshr_limit()        const { return m_mshr_limit; }
    int data_packet_flits() const { return m_profile.model.data_packet_flits; }
    int ctrl_packet_flits() const { return m_profile.model.ctrl_packet_flits; }
    bool no_mshr()          const { return m_ablation.no_mshr; }

    // Per-router injection probability for the current phase.
    // Returns 0 for nodes not in per_router_injection (non-CPU nodes).
    double get_injection_prob(int router_id) const;

    // data_pct / 100 for the current phase.
    double get_data_frac() const;

    // Weighted vnet selection using current phase's vnet_packets.
    // Returns 0, 1, or 2.
    int select_vnet(std::mt19937& rng,
                    std::uniform_real_distribution<double>& dist) const;

    // Weighted directory selection using current phase's dir_fractions.
    // Returns the dir_id.
    int select_dest_dir(std::mt19937& rng,
                        std::uniform_real_distribution<double>& dist) const;

    // Map dir_id -> router_id in the current target topology.
    int dir_to_router(int dir_id) const;

    // Select response size (1 or data_packet_flits) using correlated sizing.
    int select_response_size(std::mt19937& rng,
                             std::uniform_real_distribution<double>& dist) const;

    // ---- Metric recording (called by TGs) ----

    void record_packet_received(uint64_t latency, int phase_idx,
                                int vnet, int num_flits);
    void record_mshr_sample(int node_id, int count);

    // ---- Access to TGs (for main.cc stats loop) ----
    const std::vector<PaceTrafficGenerator*>& getTGs() const { return m_tgs; }

    // ---- Output ----
    // Write a JSON results file after simulation completes.
    void dump_results(const std::string& path,
                      const std::vector<NetworkLink*>& links,
                      uint64_t total_cycles) const;

    // Override directory remapping from profile (call before init()).
    // remap[dir_id] = router_id in the target topology.
    void set_directory_remapping(const std::map<int,int>& remap);

private:
    const PacePhase& current_phase() const {
        // Clamp to the last valid phase; safe during drain window when m_done=true.
        int idx = m_current_phase < (int)m_profile.phases.size()
                  ? m_current_phase : (int)m_profile.phases.size() - 1;
        return m_profile.phases[idx];
    }

    PaceProfile    m_profile;
    int            m_current_phase;
    uint64_t       m_cycles_in_phase;
    bool           m_done;
    int            m_mshr_limit;
    int            m_seed;
    int                m_num_routers;   // set in init(); used by no_remap
    PaceAblationConfig m_ablation;

    std::vector<PaceTrafficGenerator*> m_tgs;

    // ---- Global metrics ----
    // Sparse latency histogram: latency (cycles) -> count.
    std::map<uint64_t, uint64_t> m_latency_histogram;
    std::vector<PhaseMetrics>    m_phase_metrics;
    uint64_t m_total_latency_sum;
    uint64_t m_total_packets_received;
    uint64_t m_total_flits_received;

    // MSHR saturation tracking: running sum and sample count per node.
    std::vector<double>   m_mshr_sum;          // per node
    std::vector<uint64_t> m_mshr_sample_count; // per node
    std::vector<int>      m_max_mshr_per_node; // per node
};

} // namespace garnet

#endif // __PACE_ADAPTER_HH__

// PACE Traffic Generator — declaration only.
// Implementations are in PaceAdapter.cc (after PaceAdapter is fully defined).

#ifndef __PACE_TRAFFIC_GENERATOR_HH__
#define __PACE_TRAFFIC_GENERATOR_HH__

#include <queue>
#include <random>
#include <vector>
#include <cstdint>
#include <cassert>

#include "flit.hh"
#include "GarnetNetwork.hh"
#include "NetworkInterface.hh"
#include "CommonTypes.hh"
#include "PaceProfile.hh"
#include "TrafficGenerator.hh"

namespace garnet {

class PaceAdapter; // defined in PaceAdapter.hh

class PaceTrafficGenerator : public TrafficGenerator {
public:
    // A queued response to generate on behalf of a directory.
    struct ResponseJob {
        int      dest_ni;
        int      dest_router;
        int      num_flits;
        uint64_t creation_time;
    };

    PaceTrafficGenerator(int id, GarnetNetwork* net, NetworkInterface* ni,
                         PaceAdapter* adapter,
                         bool is_core, bool is_directory,
                         int mshr_limit, int seed);
    ~PaceTrafficGenerator();

    // --- Interface called by NetworkInterface::wakeup() ---
    flit* send_flit()              override;
    void  receive_flit(flit* flt)  override;
    void  requeue_flit(flit* flt)  override { m_stalled_flit = flt; }

    // --- Stats (override TrafficGenerator pure virtuals; non-const to match) ---
    uint64_t get_total_latency()      override { return m_total_latency; }
    uint64_t get_received_packets()   override { return m_received_packets; }
    uint64_t get_injected_packets()   override { return m_injected_packets; }
    uint64_t get_injection_attempts() override { return m_injection_attempts; }
    uint64_t get_received_vnet(int v) override {
        return (size_t)v < m_received_per_vnet.size() ? m_received_per_vnet[v] : 0;
    }
    uint64_t get_latency_vnet(int v) override {
        return (size_t)v < m_latency_per_vnet.size() ? m_latency_per_vnet[v] : 0;
    }
    // PACE-specific (not in base class).
    int  get_mshr_count()     const { return m_mshr_count; }
    int  get_max_mshr_count() const { return m_max_mshr_count; }
    bool is_core()            const { return m_is_core; }
    bool is_directory()       const { return m_is_directory; }

    // --- Setters (override base; most are no-ops in PACE mode) ---
    void set_packet_size(int)       override {}
    void set_active(bool)           override {}
    void set_injection_rate(double) override {}
    void set_trace_packet(bool t)   override { m_trace = t; }
    void set_seed(int seed)         override { m_rng.seed(seed + m_id); }
    void schedule_next_injection(uint64_t) override {}
    uint64_t get_next_injection_time() const override { return 0; }

    // Exposed for adapter use
    std::mt19937& rng() { return m_rng; }
    std::uniform_real_distribution<double>& dist() { return m_dist; }

private:
    // Implementations defined in PaceAdapter.cc
    void generate_packet(int dest_ni, int dest_router, int vnet,
                         int num_flits, uint64_t time);
    uint64_t current_time() const;

    // --- State ---
    int               m_id;
    GarnetNetwork*    m_net_ptr;
    NetworkInterface* m_ni;
    PaceAdapter*      m_adapter;

    bool m_is_core;
    bool m_is_directory;
    int  m_mshr_limit;
    int  m_mshr_count;
    int  m_max_mshr_count;

    std::queue<flit*>         m_flit_queue;      // current outbound packet
    flit*                     m_stalled_flit;    // requeued if NI VC was full
    std::queue<ResponseJob>   m_pending_responses;

    uint64_t m_last_injection_cycle;
    uint64_t m_total_latency;
    uint64_t m_received_packets;
    uint64_t m_injected_packets;
    uint64_t m_injection_attempts;

    std::vector<uint64_t> m_received_per_vnet;
    std::vector<uint64_t> m_latency_per_vnet;

    bool m_trace;
    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_dist;
};

} // namespace garnet

#endif // __PACE_TRAFFIC_GENERATOR_HH__

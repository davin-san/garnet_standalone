// Abstract base class for all traffic generators.
// Allows NetworkInterface to hold either SimpleTrafficGenerator
// or PaceTrafficGenerator through the same pointer.

#ifndef __TRAFFIC_GENERATOR_HH__
#define __TRAFFIC_GENERATOR_HH__

#include <cstdint>
#include "flit.hh"
#include "StandaloneStats.hh"

namespace garnet {

class TrafficGenerator {
public:
    virtual ~TrafficGenerator() = default;

    // Core injection/ejection interface called by NetworkInterface::wakeup().
    virtual flit*    send_flit()          = 0;
    virtual void     receive_flit(flit*)  = 0;
    virtual void     requeue_flit(flit*)  = 0;

    // Configuration setters.
    virtual void     set_packet_size(int)       = 0;
    virtual void     set_active(bool)           = 0;
    virtual void     set_injection_rate(double) = 0;
    virtual void     set_trace_packet(bool)     = 0;
    virtual void     set_seed(int)              = 0;

    // Scheduling (may be no-ops).
    virtual void     schedule_next_injection(uint64_t) = 0;
    virtual uint64_t get_next_injection_time() const   = 0;

    // Statistics.
    virtual uint64_t get_total_latency()      = 0;
    virtual uint64_t get_received_packets()   = 0;
    virtual uint64_t get_injected_packets()   = 0;
    virtual uint64_t get_injection_attempts() = 0;
    virtual uint64_t get_received_vnet(int)   = 0;
    virtual uint64_t get_latency_vnet(int)    = 0;

    // Latency histogram — overridden by SimpleTrafficGenerator.
    // PaceTrafficGenerator keeps the histogram in PaceAdapter; returns empty here.
    virtual const LatHist& get_lat_hist() const {
        static const LatHist empty;
        return empty;
    }
};

} // namespace garnet

#endif // __TRAFFIC_GENERATOR_HH__

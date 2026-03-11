#ifndef __GARNET_STATS_HH__
#define __GARNET_STATS_HH__

#include <iostream>
#include <vector>
#include <cstdint>

namespace garnet {

// NUM_STAT_VNETS covers vnets 0 (request), 1 (response), 2 (writeback).
static const int NUM_STAT_VNETS = 3;

struct GarnetStats {
    uint64_t injected_packets[NUM_STAT_VNETS]        = {0, 0, 0};
    uint64_t received_packets[NUM_STAT_VNETS]        = {0, 0, 0};
    uint64_t packet_network_latency[NUM_STAT_VNETS]  = {0, 0, 0};
    uint64_t packet_queueing_latency[NUM_STAT_VNETS] = {0, 0, 0};
    uint64_t injected_flits[NUM_STAT_VNETS]          = {0, 0, 0};
    uint64_t received_flits[NUM_STAT_VNETS]          = {0, 0, 0};
    uint64_t flit_network_latency[NUM_STAT_VNETS]    = {0, 0, 0};
    uint64_t flit_queueing_latency[NUM_STAT_VNETS]   = {0, 0, 0};
    uint64_t total_hops = 0;

    void reset() {
        for (int i = 0; i < NUM_STAT_VNETS; ++i) {
            injected_packets[i] = 0;
            received_packets[i] = 0;
            packet_network_latency[i] = 0;
            packet_queueing_latency[i] = 0;
            injected_flits[i] = 0;
            received_flits[i] = 0;
            flit_network_latency[i] = 0;
            flit_queueing_latency[i] = 0;
        }
        total_hops = 0;
    }
    
    void print(std::ostream& out) const {
        out << "Global Simulation Statistics:\n";

        uint64_t total_pkt_in = 0, total_pkt_out = 0;
        for (int i = 0; i < NUM_STAT_VNETS; ++i) {
            total_pkt_in  += injected_packets[i];
            total_pkt_out += received_packets[i];
        }
        
        out << "  Packets Injected: " << total_pkt_in << "\n";
        out << "  Packets Received: " << total_pkt_out << "\n";
        
        if (total_pkt_out > 0) {
            uint64_t net_lat_sum = 0, queue_lat_sum = 0;
            for (int i = 0; i < NUM_STAT_VNETS; ++i) {
                net_lat_sum   += packet_network_latency[i];
                queue_lat_sum += packet_queueing_latency[i];
            }
            out << "  Average Packet Latency: "
                << (double)(net_lat_sum + queue_lat_sum) / total_pkt_out << " cycles\n";
            out << "  Average Network Latency: "
                << (double)net_lat_sum   / total_pkt_out << " cycles\n";
            out << "  Average Queueing Latency: "
                << (double)queue_lat_sum / total_pkt_out << " cycles\n";
            out << "  Average Hops: "
                << (double)total_hops    / total_pkt_out << "\n";
        }
    }
};

} // namespace garnet

#endif // __GARNET_STATS_HH__
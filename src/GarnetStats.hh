#ifndef __GARNET_STATS_HH__
#define __GARNET_STATS_HH__

#include <iostream>
#include <vector>
#include <cstdint>

namespace garnet {

struct GarnetStats {
    uint64_t injected_packets[2] = {0, 0};
    uint64_t received_packets[2] = {0, 0};
    uint64_t packet_network_latency[2] = {0, 0};
    uint64_t packet_queueing_latency[2] = {0, 0};
    uint64_t injected_flits[2] = {0, 0};
    uint64_t received_flits[2] = {0, 0};
    uint64_t flit_network_latency[2] = {0, 0};
    uint64_t flit_queueing_latency[2] = {0, 0};
    uint64_t total_hops = 0;

    void reset() {
        for(int i=0; i<2; ++i) {
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
        
        uint64_t total_pkt_in = injected_packets[0] + injected_packets[1];
        uint64_t total_pkt_out = received_packets[0] + received_packets[1];
        
        out << "  Packets Injected: " << total_pkt_in << "\n";
        out << "  Packets Received: " << total_pkt_out << "\n";
        
        if (total_pkt_out > 0) {
            double avg_lat = (double)(packet_network_latency[0] + packet_network_latency[1] + 
                                      packet_queueing_latency[0] + packet_queueing_latency[1]) / total_pkt_out;
            out << "  Average Packet Latency: " << avg_lat << " cycles\n";
            
            double avg_net_lat = (double)(packet_network_latency[0] + packet_network_latency[1]) / total_pkt_out;
             out << "  Average Network Latency: " << avg_net_lat << " cycles\n";
             
            double avg_queue_lat = (double)(packet_queueing_latency[0] + packet_queueing_latency[1]) / total_pkt_out;
            out << "  Average Queueing Latency: " << avg_queue_lat << " cycles\n";
            
            double avg_hops = (double)total_hops / total_pkt_out;
            out << "  Average Hops: " << avg_hops << "\n";
        }
    }
};

} // namespace garnet

#endif // __GARNET_STATS_HH__
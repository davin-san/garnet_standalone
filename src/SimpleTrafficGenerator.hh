#ifndef __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__
#define __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__

#include <queue>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <iostream>
#include <random>
#include <vector>
#include "flit.hh"
#include "GarnetNetwork.hh"
#include "NetworkInterface.hh"
#include "CommonTypes.hh"
#include "NetDest.hh"

namespace garnet
{

class NetworkInterface;

class SimpleTrafficGenerator
{
public:
    SimpleTrafficGenerator(int id, int num_nis, double injection_rate,
                           GarnetNetwork* net_ptr, NetworkInterface* ni)
        : m_id(id), m_num_nis(num_nis), m_injection_rate(injection_rate),
          m_net_ptr(net_ptr), m_ni(ni), m_stalled_flit(nullptr),
          m_total_latency(0), m_received_packets(0),
          m_injected_packets(0), m_injection_attempts(0),
          m_dist(0.0, 1.0)
    {
        m_active = true;
        m_packet_size = 1;
        m_trace_packet = false;
        m_rng.seed(42 + id);
        int num_vnets = 2; // Default from Topology
        m_received_per_vnet.resize(num_vnets, 0);
        m_latency_per_vnet.resize(num_vnets, 0);
        m_dest_dist = std::uniform_int_distribution<int>(0, num_nis - 1);
        m_vnet_dist = std::uniform_int_distribution<int>(0, num_vnets - 1);
    }

    ~SimpleTrafficGenerator()
    {
        if (m_stalled_flit) delete m_stalled_flit;
        while (!m_flit_queue.empty()) {
            flit* fl = m_flit_queue.front();
            m_flit_queue.pop();
            delete fl;
        }
    }

    void set_injection_rate(double rate) { m_injection_rate = rate; }
    void set_packet_size(int size) { m_packet_size = size; }
    void set_active(bool active) { m_active = active; }
    void set_seed(int seed) { m_rng.seed(seed + m_id); }
    void set_trace_packet(bool trace) { m_trace_packet = trace; }

    flit* send_flit()
    {
        m_injection_attempts++; 
        if (m_stalled_flit) {
            flit* fl = m_stalled_flit;
            m_stalled_flit = nullptr;
            return fl;
        }
        
        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

        if (m_active && m_id == 0 && m_flit_queue.empty()) {
            int dest_id = m_num_nis - 1; // Send to last NI
            generate_packet(dest_id, 0, current_time, m_trace_packet);
            m_injected_packets++;
        }
        else if (!m_active && m_injection_rate > 0.0) {
            if (m_dist(m_rng) <= m_injection_rate) {
                int dest_id = m_dest_dist(m_rng);
                if (dest_id == m_id) dest_id = (dest_id + 1) % m_num_nis;
                int vnet = m_vnet_dist(m_rng);
                generate_packet(dest_id, vnet, current_time);
                m_injected_packets++;
            }
        }

        if (!m_flit_queue.empty()) {
            flit* head = m_flit_queue.front();
            m_flit_queue.pop();
            head->set_enqueue_time(current_time);
            return head;
        }
        return nullptr;
    }

    void requeue_flit(flit* flt) { m_stalled_flit = flt; }

    void receive_flit(flit* flt) {
        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();
        if (flt->get_type() == TAIL_ || flt->get_type() == HEAD_TAIL_) {
            uint64_t latency = current_time - flt->get_enqueue_time();
            m_total_latency += latency;
            m_received_packets++;
            int vnet = flt->get_vnet();
            if (vnet < (int)m_received_per_vnet.size()) {
                m_received_per_vnet[vnet]++;
                m_latency_per_vnet[vnet] += latency;
            }
        }
        delete flt; 
    }

    uint64_t get_total_latency() { return m_total_latency; }
    uint64_t get_received_packets() { return m_received_packets; }
    uint64_t get_injected_packets() { return m_injected_packets; }
    uint64_t get_injection_attempts() { return m_injection_attempts; }
    
    uint64_t get_received_vnet(int vnet) { return m_received_per_vnet[vnet]; }
    uint64_t get_latency_vnet(int vnet) { return m_latency_per_vnet[vnet]; }

    // Dummy for NI compatibility
    void schedule_next_injection(uint64_t t) {}
    uint64_t get_next_injection_time() const { return 0; }

private:
    void generate_packet(int dest_id, int vnet, uint64_t time, bool trace = false) {
        if (!m_ni) {
            std::cerr << "Error: m_ni is null in SimpleTrafficGenerator " << m_id << std::endl;
            return;
        }
        int packet_size = m_packet_size; 
        int packet_id = m_net_ptr->getNextPacketID();
        uint32_t ni_flit_size = m_net_ptr->getNiFlitSize();

        if (trace) {
            std::cout << "TRACE: Packet " << packet_id << " generating at NI " << m_id 
                      << " for NI " << dest_id << " at time " << time << std::endl;
        }

        RouteInfo route;
        route.src_ni = m_id;
        route.dest_ni = dest_id;
        route.src_router = m_ni->get_router_id(vnet);
        route.dest_router = m_net_ptr->get_router_id(dest_id, vnet); 
        route.vnet = vnet;
        route.net_dest.add(dest_id);

        for (int i = 0; i < packet_size; i++) {
            flit* fl = new flit(packet_id, i, 0, vnet, route, packet_size,
                                nullptr, 0, ni_flit_size, time);
            fl->set_trace(trace);
            m_flit_queue.push(fl);
        }
    }

    int m_id;
    int m_num_nis;
    double m_injection_rate;
    int m_packet_size = 1;
    GarnetNetwork* m_net_ptr;
    NetworkInterface* m_ni;
    std::queue<flit*> m_flit_queue; 
    flit* m_stalled_flit;
    bool m_active; 
    bool m_trace_packet = false;

    uint64_t m_total_latency;
    uint64_t m_received_packets;
    uint64_t m_injected_packets;
    uint64_t m_injection_attempts;
    
    std::vector<uint64_t> m_received_per_vnet;
    std::vector<uint64_t> m_latency_per_vnet;

    std::mt19937 m_rng;
    std::uniform_real_distribution<double> m_dist;
    std::uniform_int_distribution<int> m_dest_dist;
    std::uniform_int_distribution<int> m_vnet_dist;
};

} // namespace garnet

#endif // __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__

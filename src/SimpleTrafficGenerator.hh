#ifndef __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__
#define __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__

#include <queue>
#include <cstdlib> // For rand()
#include "flit.hh"
#include "GarnetNetwork.hh"
#include "NetworkInterface.hh"
#include "CommonTypes.hh"
#include "NetDest.hh" // Include NetDest header

namespace garnet
{

// Forward declaration to break circular dependency
class NetworkInterface;

class SimpleTrafficGenerator
{
public:
    SimpleTrafficGenerator(int id, int num_nis, double injection_rate,
                           GarnetNetwork* net_ptr, NetworkInterface* ni)
        : m_id(id), m_num_nis(num_nis), m_injection_rate(injection_rate),
          m_net_ptr(net_ptr), m_ni(ni), m_stalled_flit(nullptr) // Init stall
    {
        // Seed with ID for deterministic (but different) random streams
        srand(id);
        m_active = true;
    }

    ~SimpleTrafficGenerator()
    {
        // Clean up a stalled flit if one exists
        if (m_stalled_flit) {
            if (m_stalled_flit->get_type() == TAIL_ ||
                m_stalled_flit->get_type() == HEAD_TAIL_) {
                 delete m_stalled_flit->get_route().net_dest;
            }
            delete m_stalled_flit;
        }
        
        // Clean up any flits created but not yet sent
        while (!m_flit_queue.empty()) {
            flit* fl = m_flit_queue.front();
            m_flit_queue.pop();
            if (fl->get_type() == TAIL_ || fl->get_type() == HEAD_TAIL_) {
                 delete fl->get_route().net_dest;
            }
            delete fl;
        }
    }

    void set_injection_rate(double rate) { m_injection_rate = rate; }
    void set_packet_size(int size) { m_packet_size = size; }
    void set_active(bool active) { m_active = active; }

    // Called by NI::wakeup() to get a new flit
    flit* send_flit()
    {
        // Handle stalls
        if (m_stalled_flit) {
            flit* fl = m_stalled_flit;
            m_stalled_flit = nullptr; // Clear stall
            return fl; // Retry sending
        }
        
        // Check if we are already in the middle of sending a packet
        if (!m_flit_queue.empty()) {
            flit* fl = m_flit_queue.front();
            m_flit_queue.pop();
            return fl;
        }

        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

        // --- DETERMINISTIC TEST PATTERN ---
        // Packet 1: Node 0 -> Node 3 at Cycle 1
        if (m_active && m_id == 0 && current_time == 1) {
            m_active = false; // Only send once
            
            int dest_id = 3; // 0 -> 3 (Diagonal in 2x2)
            
            // Define packet properties
            int packet_size = m_packet_size; 
            int vnet = 0; 
            int packet_id = m_net_ptr->getNextPacketID();
            uint32_t ni_flit_size = m_net_ptr->getNiFlitSize();

            RouteInfo route;
            route.src_ni = m_id;
            route.dest_ni = dest_id;
            route.src_router = m_ni->get_router_id(vnet);
            route.dest_router = dest_id; 
            route.net_dest = new NetDest(); 

            // Create flits
            for (int i = 0; i < packet_size; i++) {
                flit* fl = new flit(packet_id, i, 0, vnet, route, packet_size,
                                    nullptr, 0, ni_flit_size, current_time);
                m_flit_queue.push(fl);
            }

            flit* head_flit = m_flit_queue.front();
            m_flit_queue.pop();
            
            std::cout << "[Cycle " << current_time << "] GEN: Generated Packet " 
                      << packet_id << " from " << m_id << " to " << dest_id << std::endl;
            
            return head_flit;
        }

        // Random Injection (if not deterministic test)
        // If m_active is true, we assume deterministic test mode for now based on logic above.
        // If we want random, we should separate flags. 
        // For this refactor, let's keep it simple: 
        // If injection_rate > 0 and NOT active (test mode), do random.
        
        if (!m_active && m_injection_rate > 0.0) {
             double random_val = (double)rand() / RAND_MAX;
             if (random_val <= m_injection_rate) {
                 // Create Random Packet
                 int dest_id = rand() % m_num_nis;
                 if (dest_id == m_id) dest_id = (dest_id + 1) % m_num_nis;
                 
                 int packet_size = m_packet_size;
                 int vnet = 0;
                 int packet_id = m_net_ptr->getNextPacketID();
                 uint32_t ni_flit_size = m_net_ptr->getNiFlitSize();
                 
                 RouteInfo route;
                 route.src_ni = m_id;
                 route.dest_ni = dest_id;
                 route.src_router = m_ni->get_router_id(vnet);
                 route.dest_router = dest_id;
                 route.net_dest = new NetDest();

                 for (int i = 0; i < packet_size; i++) {
                     flit* fl = new flit(packet_id, i, 0, vnet, route, packet_size,
                                         nullptr, 0, ni_flit_size, current_time);
                     m_flit_queue.push(fl);
                 }
                 
                 flit* head = m_flit_queue.front();
                 m_flit_queue.pop();
                 return head;
             }
        }

        return nullptr;
    }

    void requeue_flit(flit* flt)
    {
        assert(m_stalled_flit == nullptr);
        m_stalled_flit = flt;
    }

    void receive_flit(flit* flt)
    {
        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();
        if (flt->get_type() == TAIL_ || flt->get_type() == HEAD_TAIL_) {
            std::cout << "[Cycle " << current_time << "] CONSUMER: NI " << m_id 
                      << " received TAIL of Packet " << flt->getPacketID() 
                      << ". Latency: " << (current_time - flt->get_enqueue_time()) << " cycles." << std::endl;
            delete flt->get_route().net_dest;
        }
        delete flt; 
    }

private:
    int m_id;
    int m_num_nis;
    double m_injection_rate;
    int m_packet_size = 1; // Default
    GarnetNetwork* m_net_ptr;
    NetworkInterface* m_ni;
    std::queue<flit*> m_flit_queue; 
    flit* m_stalled_flit;
    bool m_active; // Used for deterministic test mode
};

} // namespace garnet

#endif // __GARNET_SIMPLE_TRAFFIC_GENERATOR_HH__
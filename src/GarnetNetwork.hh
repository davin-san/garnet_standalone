/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __GARNET_NETWORK_HH__
#define __GARNET_NETWORK_HH__

#include <iostream>
#include <vector>

#include "CommonTypes.hh"
#include "EventQueue.hh"
#include "GarnetStats.hh"
#include "FaultModel.hh"
#include "NetDest.hh"

namespace garnet
{

class NetworkInterface;
class Router;
class NetworkLink;
class NetworkBridge;
class CreditLink;

// A placeholder for the configuration parameters.
// This will be replaced by a proper configuration system (e.g., JSON).
struct GarnetNetworkParams {
    uint32_t num_rows;
    uint32_t num_cols;
    uint32_t num_depth;
    uint32_t ni_flit_size;
    uint32_t vcs_per_vnet;
    uint32_t buffers_per_data_vc;
    uint32_t buffers_per_ctrl_vc;
    int routing_algorithm;
    bool enable_fault_model;
    bool enable_debug;
    // Add other parameters as needed
};


class GarnetNetwork
{
  public:
    typedef GarnetNetworkParams Params;
    GarnetNetwork(const Params &p);
    ~GarnetNetwork();

    void init();

    EventQueue* getEventQueue() { return &m_event_queue; }

    const char *garnetVersion = "3.0";

    // Configuration (set externally)

    // for 2D topology
    int getNumRows() const { return m_num_rows; }
    int getNumCols() const { return m_num_cols; }
    int getNumDepth() const { return m_num_depth; }

    // for network
    uint32_t getNiFlitSize() const { return m_ni_flit_size; }
    uint32_t getBuffersPerDataVC() { return m_buffers_per_data_vc; }
    uint32_t getBuffersPerCtrlVC() { return m_buffers_per_ctrl_vc; }
    int getRoutingAlgorithm() const { return m_routing_algorithm; }
    bool getDebug() const { return m_debug; }

    bool isFaultModelEnabled() const { return m_enable_fault_model; }
    FaultModel* fault_model;


    // Internal configuration
    VNET_type
    get_vnet_type(int vnet)
    {
        return m_vnet_type[vnet];
    }
    int getNumRouters();
    int get_router_id(int ni, int vnet);
    void registerNI(NetworkInterface* ni) { m_nis.push_back(ni); }
    void registerRouter(Router* router) { m_routers.push_back(router); }

    // Stats
    void print(std::ostream& out) const;

    void increment_injected_packets(int vnet) { m_garnetStats.injected_packets[vnet]++; }
    void increment_received_packets(int vnet) { m_garnetStats.received_packets[vnet]++; }
    void increment_packet_network_latency(uint64_t latency, int vnet) { m_garnetStats.packet_network_latency[vnet] += latency; }
    void increment_packet_queueing_latency(uint64_t latency, int vnet) { m_garnetStats.packet_queueing_latency[vnet] += latency; }
    void increment_injected_flits(int vnet) { m_garnetStats.injected_flits[vnet]++; }
    void increment_received_flits(int vnet) { m_garnetStats.received_flits[vnet]++; }
    void increment_flit_network_latency(uint64_t latency, int vnet) { m_garnetStats.flit_network_latency[vnet] += latency; }
    void increment_flit_queueing_latency(uint64_t latency, int vnet) { m_garnetStats.flit_queueing_latency[vnet] += latency; }
    void increment_total_hops(int hops) { m_garnetStats.total_hops += hops; }

    void update_traffic_distribution(RouteInfo route);
    int getNextPacketID() { return m_next_packet_id++; }

    bool isVNetOrdered(int vnet) { return vnet == 0; } // gem5 defaults VNet 0 to ordered

    GarnetStats& getStats() { return m_garnetStats; }

  protected:
    // Configuration
    int m_num_rows;
    int m_num_cols;
    int m_num_depth;
    uint32_t m_ni_flit_size;
    uint32_t m_max_vcs_per_vnet;
    uint32_t m_buffers_per_ctrl_vc;
    uint32_t m_buffers_per_data_vc;
    int m_routing_algorithm;
    bool m_enable_fault_model;
    bool m_debug;

    // Statistical variables
    GarnetStats m_garnetStats;

  private:
    GarnetNetwork(const GarnetNetwork& obj);
    GarnetNetwork& operator=(const GarnetNetwork& obj);

    EventQueue m_event_queue;
    std::vector<bool> m_ordered;
    std::vector<VNET_type > m_vnet_type;
    std::vector<Router *> m_routers;   // All Routers in Network
    std::vector<NetworkLink *> m_networklinks; // All flit links in the network
    std::vector<NetworkBridge *> m_networkbridges; // All network bridges
    std::vector<CreditLink *> m_creditlinks; // All credit links in the network
    std::vector<NetworkInterface *> m_nis;   // All NI's in Network
    int m_next_packet_id; // static vairable for packet id allocation
};

inline std::ostream&
operator<<(std::ostream& out, const GarnetNetwork& obj)
{
    obj.print(out);
    out << std::flush;
    return out;
}

} // namespace garnet

#endif //__GARNET_NETWORK_HH__
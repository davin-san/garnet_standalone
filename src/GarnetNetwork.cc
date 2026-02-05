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


#include "GarnetNetwork.hh"

#include <cassert>

#include "NetDest.hh"
#include "CommonTypes.hh"
#include "CreditLink.hh"
#include "GarnetLink.hh"
#include "NetworkInterface.hh"
#include "NetworkLink.hh"
#include "Router.hh"


namespace garnet
{

/*
 * GarnetNetwork sets up the routers and links and collects stats.
 * Default parameters (GarnetNetwork.py) can be overwritten from command line
 * (see configs/network/Network.py)
 */

GarnetNetwork::GarnetNetwork(const Params &p)
{
    m_num_rows = p.num_rows;
    m_num_cols = p.num_cols;
    m_ni_flit_size = p.ni_flit_size;
    m_max_vcs_per_vnet = 0;
    m_buffers_per_data_vc = p.buffers_per_data_vc;
    m_buffers_per_ctrl_vc = p.buffers_per_ctrl_vc;
    m_routing_algorithm = p.routing_algorithm;
    m_next_packet_id = 0;
    m_debug = p.enable_debug;

    m_enable_fault_model = p.enable_fault_model;
    if (m_enable_fault_model)
        fault_model = nullptr; // new FaultModel(p.fault_model);

    // m_vnet_type.resize(m_virtual_networks);

    // for (int i = 0 ; i < m_virtual_networks ; i++) {
    //     if (m_vnet_type_names[i] == "response")
    //         m_vnet_type[i] = DATA_VNET_; // carries data (and ctrl) packets
    //     else
    //         m_vnet_type[i] = CTRL_VNET_; // carries only ctrl packets
    // }

    // record the routers
    // for (std::vector<BasicRouter*>::const_iterator i =  p.routers.begin();
    //      i != p.routers.end(); ++i) {
    //     Router* router = safe_cast<Router*>(*i);
    //     m_routers.push_back(router);

    //     // initialize the router's network pointers
    //     router->init_net_ptr(this);
    // }

    // // record the network interfaces
    // for (std::vector<ClockedObject*>::const_iterator i = p.netifs.begin();
    //      i != p.netifs.end(); ++i) {
    //     NetworkInterface *ni = safe_cast<NetworkInterface *>(*i);
    //     m_nis.push_back(ni);
    //     ni->init_net_ptr(this);
    // }

    // Print Garnet version
    // inform("Garnet version %s\n", garnetVersion);
}

void
GarnetNetwork::init()
{
    // This will be handled by the configuration system
}

/*
 * This function creates a link from the Network Interface (NI)
 * into the Network.
 * It creates a Network Link from the NI to a Router and a Credit Link from
 * the Router to the NI
*/

void
GarnetNetwork::makeExtInLink(NodeID global_src, SwitchID dest,
                             std::vector<NetDest>& routing_table_entry)
{
    // Will be re-implemented
}

/*
 * This function creates a link from the Network to a NI.
 * It creates a Network Link from a Router to the NI and
 * a Credit Link from NI to the Router
*/

void
GarnetNetwork::makeExtOutLink(SwitchID src, NodeID global_dest,
                              std::vector<NetDest>& routing_table_entry)
{
    // Will be re-implemented
}

/*
 * This function creates an internal network link between two routers.
 * It adds both the network link and an opposite credit link.
*/

void
GarnetNetwork::makeInternalLink(SwitchID src, SwitchID dest,
                                std::vector<NetDest>& routing_table_entry,
                                PortDirection src_outport_dirn,
                                PortDirection dst_inport_dirn)
{
    // Will be re-implemented
}

// Total routers in the network
int
GarnetNetwork::getNumRouters()
{
    return m_routers.size();
}

// Get ID of router connected to a NI.
int
GarnetNetwork::get_router_id(int global_ni, int vnet)
{
    return m_nis[global_ni]->get_router_id(vnet);
}


void
GarnetNetwork::print(std::ostream& out) const
{
    out << "[GarnetNetwork]";
}

// The following methods are removed for the standalone version
// void GarnetNetwork::regStats() {}
// void GarnetNetwork::collateStats() {}
// void GarnetNetwork::resetStats() {}
// void GarnetNetwork::update_traffic_distribution(RouteInfo route) {}
// bool GarnetNetwork::functionalRead(Packet *pkt, WriteMask &mask) { return false; }
// uint32_t GarnetNetwork::functionalWrite(Packet *pkt) { return 0; }

} // namespace garnet

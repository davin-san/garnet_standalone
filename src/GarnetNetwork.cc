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
#include "FaultModel.hh"


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
    m_num_depth = p.num_depth;
    m_ni_flit_size = p.ni_flit_size;
    m_max_vcs_per_vnet = 0;
    m_buffers_per_data_vc = p.buffers_per_data_vc;
    m_buffers_per_ctrl_vc = p.buffers_per_ctrl_vc;
    m_routing_algorithm = p.routing_algorithm;
    m_next_packet_id = 0;
    m_debug = p.enable_debug;

    m_enable_fault_model = p.enable_fault_model;
    if (m_enable_fault_model)
        fault_model = new FaultModel();
    else
        fault_model = nullptr;
    
    m_garnetStats.reset();
}

GarnetNetwork::~GarnetNetwork()
{
    if (fault_model)
        delete fault_model;
}

void
GarnetNetwork::init()
{
    if (m_enable_fault_model) {
        for (std::vector<Router*>::const_iterator i = m_routers.begin();
             i != m_routers.end(); ++i) {
            Router* router = *i;
            int router_id =
                fault_model->declare_router(router->get_num_inports(),
                                            router->get_num_outports(),
                                            router->get_vc_per_vnet(),
                                            getBuffersPerDataVC(),
                                            getBuffersPerCtrlVC());
            assert(router_id == router->get_id());
            // router->printAggregateFaultProbability(std::cout);
            // router->printFaultVector(std::cout);
        }
    }
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
    out << "[GarnetNetwork]\n";
    m_garnetStats.print(out);
}

void GarnetNetwork::update_traffic_distribution(RouteInfo route) {}

} // namespace garnet
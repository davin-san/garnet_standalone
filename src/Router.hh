/*
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
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


#ifndef __GARNET_ROUTER_HH__
#define __GARNET_ROUTER_HH__

#include <iostream>
#include <memory>
#include <vector>

#include "CommonTypes.hh"
#include "GarnetSimObject.hh"
#include "Consumer.hh"
#include "flit.hh"

namespace garnet
{

class NetworkLink;
class CreditLink;
class InputUnit;
class OutputUnit;
class GarnetNetwork;
class NetDest;
class CrossbarSwitch;
class RoutingUnit;
class SwitchAllocator;


struct GarnetRouterParams {
    int id;
    int x, y, z;
    int virtual_networks;
    int vcs_per_vnet;
    uint64_t latency;
    GarnetNetwork *network_ptr;
};

class Router : public Consumer
{
  public:
    typedef GarnetRouterParams Params;
    Router(const Params &p);

    ~Router();

    void wakeup();
    void print(std::ostream &out) const {};

    void init();
    void addInPort(PortDirection inport_dirn, NetworkLink *link,
                   CreditLink *credit_link);
    void addOutPort(PortDirection outport_dirn, NetworkLink *link,
                    std::vector<NetDest> &routing_table_entry,
                    int link_weight, CreditLink *credit_link,
                    uint32_t consumerVcs);

    void scheduleEvent(uint64_t time) override;

    uint64_t get_pipe_stages() { return m_latency; }
    uint32_t get_num_vcs() { return m_num_vcs; }
    uint32_t get_num_vnets() { return m_virtual_networks; }
    uint32_t get_vc_per_vnet() { return m_vc_per_vnet; }
    int get_num_inports() { return m_input_unit.size(); }
    int get_num_outports() { return m_output_unit.size(); }
    int get_id() { return m_id; }

    int get_x() const { return m_x; }
    int get_y() const { return m_y; }
    int get_z() const { return m_z; }

    GarnetNetwork *get_net_ptr() { return m_network_ptr; }

    InputUnit*
    getInputUnit(unsigned port)
    {
        assert(port < m_input_unit.size());
        return m_input_unit[port].get();
    }

    OutputUnit*
    getOutputUnit(unsigned port)
    {
        assert(port < m_output_unit.size());
        return m_output_unit[port].get();
    }

    PortDirection getOutportDirection(int outport);
    PortDirection getInportDirection(int inport);

    int getOutportIndex(PortDirection dir);

    int route_compute(RouteInfo route, int inport, PortDirection direction);
    void grant_switch(int inport, flit *t_flit);

    void addRouteForPort(int port, int dest_ni);

    std::string getPortDirectionName(PortDirection direction);

    bool get_fault_vector(int temperature, float fault_vector[]);
    bool get_aggregate_fault_probability(int temperature, float *aggregate_fault_prob);
    void printFaultVector(std::ostream& out);
    void printAggregateFaultProbability(std::ostream& out);

  private:
    int m_id;
    int m_x, m_y, m_z;
    uint64_t m_latency;
    uint32_t m_virtual_networks, m_vc_per_vnet, m_num_vcs;
    GarnetNetwork *m_network_ptr;

    RoutingUnit* m_routing_unit;
    SwitchAllocator* m_sw_alloc;
    CrossbarSwitch* m_crossbar_switch;

    std::vector<std::shared_ptr<InputUnit>> m_input_unit;
    std::vector<std::shared_ptr<OutputUnit>> m_output_unit;
};

} // namespace garnet

#endif // __GARNET_ROUTER_HH__

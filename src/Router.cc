/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
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


#include "Router.hh"


#include "CreditLink.hh"
#include "GarnetNetwork.hh"
#include "InputUnit.hh"
#include "NetworkLink.hh"
#include "OutputUnit.hh"
#include "RoutingUnit.hh"
#include "SwitchAllocator.hh"
#include "CrossbarSwitch.hh"

namespace garnet
{

Router::Router(const Params &p)
  : m_id(p.id), m_latency(p.latency),
    m_virtual_networks(p.virtual_networks), m_vc_per_vnet(p.vcs_per_vnet),
    m_num_vcs(m_virtual_networks * m_vc_per_vnet),
    m_network_ptr(p.network_ptr)
{
    m_routing_unit = new RoutingUnit(this);
    m_sw_alloc = new SwitchAllocator(this);
    m_crossbar_switch = new CrossbarSwitch(this);

    m_input_unit.clear();
    m_output_unit.clear();
}

// --- ADD THIS ENTIRE FUNCTION ---
Router::~Router()
{
    delete m_routing_unit;
    delete m_sw_alloc;
    delete m_crossbar_switch;

    // m_input_unit and m_output_unit use shared_ptr,
    // so they will clean themselves up automatically.
}
// --- END OF ADDITION ---

void
Router::init()
{
    m_sw_alloc->init();
    m_crossbar_switch->init();
}

void
Router::wakeup()
{
    // check for incoming flits
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        m_input_unit[inport]->wakeup();
    }

    // check for incoming credits
    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        m_output_unit[outport]->wakeup();
    }

    // Switch Allocation
    m_sw_alloc->wakeup();

    // Switch Traversal
    m_crossbar_switch->wakeup();

    // --- FIX: Schedule self for next cycle if any InputUnit has pending flits ---
    bool has_pending_flits_in_input_units = false;
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        if (m_input_unit[inport]->has_pending_flits()) {
            has_pending_flits_in_input_units = true;
            break;
        }
    }

    if (has_pending_flits_in_input_units) {
        scheduleEvent(1);
    }
}

void
Router::addInPort(PortDirection inport_dirn,
                  NetworkLink *in_link, CreditLink *credit_link)
{
    int port_num = m_input_unit.size();
    InputUnit *input_unit = new InputUnit(port_num, inport_dirn, this);

    input_unit->set_in_link(in_link);
    input_unit->set_credit_link(credit_link);
    in_link->setLinkConsumer(this);
    in_link->setVcsPerVnet(get_vc_per_vnet());
    credit_link->setSourceQueue(input_unit->getCreditQueue());
    credit_link->setVcsPerVnet(get_vc_per_vnet());

    m_input_unit.push_back(std::shared_ptr<InputUnit>(input_unit));

    m_routing_unit->addInDirection(inport_dirn, port_num);
}

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   std::vector<NetDest>& routing_table_entry, int link_weight,
                   CreditLink *credit_link, uint32_t consumerVcs)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this,
                                             consumerVcs);

    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    credit_link->setVcsPerVnet(consumerVcs);
    out_link->setSourceQueue(output_unit->getOutQueue());
    out_link->setVcsPerVnet(consumerVcs);

    m_output_unit.push_back(std::shared_ptr<OutputUnit>(output_unit));

    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
}

PortDirection
Router::getOutportDirection(int outport)
{
    return m_output_unit[outport]->get_direction();
}

PortDirection
Router::getInportDirection(int inport)
{
    return m_input_unit[inport]->get_direction();
}

int
Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn)
{
    return m_routing_unit->outportCompute(route, inport, inport_dirn);
}

void
Router::grant_switch(int inport, flit *t_flit)
{
    m_crossbar_switch->update_sw_winner(inport, t_flit);
}

std::string
Router::getPortDirectionName(PortDirection direction)
{
    return direction;
}

void
Router::scheduleEvent(uint64_t time)
{
    m_network_ptr->getEventQueue()->schedule(this, time);
}

// The following methods are removed for the standalone version
// void Router::regStats() {}
// void Router::collateStats() {}
// void Router::resetStats() {}
// void Router::printFaultVector(std::ostream& out) {}
// void Router::printAggregateFaultProbability(std::ostream& out) {}
// bool Router::functionalRead(Packet *pkt, WriteMask &mask) { return false; }
// uint32_t Router::functionalWrite(Packet *pkt) { return 0; }

} // namespace garnet
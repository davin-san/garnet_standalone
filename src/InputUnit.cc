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


#include "InputUnit.hh"

#include "Credit.hh"
#include "Router.hh"
#include "GarnetNetwork.hh"

namespace garnet
{

InputUnit::InputUnit(int id, PortDirection direction, Router *router)
  : m_router(router), m_id(id), m_direction(direction),
    m_vc_per_vnet(m_router->get_vc_per_vnet())
{
    const int m_num_vcs = m_router->get_num_vcs();
    // Instantiating the virtual channels
    virtualChannels.reserve(m_num_vcs);
        for (int i=0; i < m_num_vcs; i++) {
            virtualChannels.emplace_back();
        }
    }
    
    // --- ADD THIS ENTIRE FUNCTION ---
    InputUnit::~InputUnit()
    {
        // Clean up any credits left in the queue
        while (!creditQueue.isEmpty()) {
            delete creditQueue.getTopFlit(); // These are Credits
        }
    }
    // --- END OF ADDITION ---
    
    
    /*
     * The InputUnit wakeup function reads the input flit from its input link.
 * Each flit arrives with an input VC.
 * For HEAD/HEAD_TAIL flits, performs route computation,
 * and updates route in the input VC.
 * The flit is buffered for (m_latency - 1) cycles in the input VC
 * and marked as valid for SwitchAllocation starting that cycle.
 *
 */

void
InputUnit::wakeup()
{
    flit *t_flit;
    uint64_t current_time = m_router->get_net_ptr()->getEventQueue()->get_current_time();
    if (m_in_link->isReady(current_time)) {

        t_flit = m_in_link->consumeLink();
        int vc = t_flit->get_vc();
        t_flit->increment_hops(); // for stats

        if (m_router->get_net_ptr()->getDebug()) {
            std::cout << "[Cycle " << current_time << "] Router " << m_router->get_id() 
                      << " RECEIVED flit " << t_flit->get_id() << " at port " << m_direction << std::endl;
        }

        if ((t_flit->get_type() == HEAD_) ||
            (t_flit->get_type() == HEAD_TAIL_)) {

            assert(virtualChannels[vc].get_state() == IDLE_);
            set_vc_active(vc, current_time);

            // Route computation for this vc
            int outport = m_router->route_compute(t_flit->get_route(),
                m_id, m_direction);

            // Update output port in VC
            // All flits in this packet will use this output port
            // The output port field in the flit is updated after it wins SA
            grant_outport(vc, outport);

        } else {
            assert(virtualChannels[vc].get_state() == ACTIVE_);
        }


        // Buffer the flit
        virtualChannels[vc].insertFlit(t_flit);

        uint64_t pipe_stages = m_router->get_pipe_stages();
        if (pipe_stages == 1) {
            // 1-cycle router
            // Flit goes for SA directly
            t_flit->advance_stage(SA_, current_time);
        } else {
            assert(pipe_stages > 1);
            // Router delay is modeled by making flit wait in buffer for
            // (pipe_stages cycles - 1) cycles before going for SA

            uint64_t wait_time = pipe_stages - 1;
            t_flit->advance_stage(SA_, current_time + wait_time);

            // Wakeup the router in that cycle to perform SA
            m_router->get_net_ptr()->getEventQueue()->schedule(m_router, wait_time);
        }

        if (m_in_link->isReady(current_time)) {
            m_router->get_net_ptr()->getEventQueue()->schedule(m_router, 1);
        }
    }
}

// Send a credit back to upstream router for this VC.
// Called by SwitchAllocator when the flit in this VC wins the Switch.
void
InputUnit::increment_credit(int in_vc, bool free_signal, uint64_t curTime)
{
    Credit *t_credit = new Credit(in_vc, free_signal, curTime);
    creditQueue.insert(t_credit);
    m_credit_link->scheduleEvent(1);
}

bool
InputUnit::has_pending_flits() const
{
    for (const auto& vc : virtualChannels) {
        if (!vc.getInputBuffer().isEmpty()) {
            return true;
        }
    }
    return false;
}


// The following methods are removed for the standalone version
// bool
// InputUnit::functionalRead(Packet *pkt, WriteMask &mask)
// {
//     bool read = false;
//     for (auto& virtual_channel : virtualChannels) {
//         if (virtual_channel.functionalRead(pkt, mask))
//             read = true;
//     }

//     return read;
// }

// uint32_t
// InputUnit::functionalWrite(Packet *pkt)
// {
//     uint32_t num_functional_writes = 0;
//     for (auto& virtual_channel : virtualChannels) {
//         num_functional_writes += virtual_channel.functionalWrite(pkt);
//     }

//     return num_functional_writes;
// }

// void
// InputUnit::resetStats()
// {
//     for (int j = 0; j < m_num_buffer_reads.size(); j++) {
//         m_num_buffer_reads[j] = 0;
//         m_num_buffer_writes[j] = 0;
//     }
// }

} // namespace garnet

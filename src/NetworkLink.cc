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


#include "NetworkLink.hh"
#include "GarnetNetwork.hh"
#include "Consumer.hh" // Added to resolve incomplete type error

#include "CreditLink.hh"
#include "flitBuffer.hh"
#include "NetDest.hh" // Added to resolve incomplete type warning

namespace garnet
{

NetworkLink::NetworkLink(const Params &p)
    : m_id(p.id),
      m_type(NUM_LINK_TYPES_),
      m_latency(p.latency), m_link_utilized(0),
      m_virt_nets(p.virtual_networks), linkBuffer(),
      link_consumer(nullptr), link_srcQueue(nullptr)
{
    m_net_ptr = p.net_ptr;
    // int num_vnets = (p.supported_vnets).size();
    // mVnets.resize(num_vnets);
    // bitWidth = p.width;
    // for (int i = 0; i < num_vnets; i++) {
    //     mVnets[i] = p.supported_vnets[i];
    // }
}

// --- ADD THIS ENTIRE FUNCTION ---
NetworkLink::~NetworkLink()
{
    while (!linkBuffer.isEmpty()) {
        flit* fl = linkBuffer.getTopFlit();
        
        // Only delete NetDest if it's not a credit
        if (fl->get_type() != CREDIT_ &&
           (fl->get_type() == TAIL_ || fl->get_type() == HEAD_TAIL_)) {
             delete fl->get_route().net_dest;
        }
        delete fl;
    }
}
// --- END OF ADDITION ---

void
NetworkLink::setLinkConsumer(Consumer *consumer)
{
    link_consumer = consumer;
}

void
NetworkLink::setVcsPerVnet(uint32_t consumerVcs)
{
    m_vc_load.resize(m_virt_nets * consumerVcs);
}

void
NetworkLink::setSourceQueue(flitBuffer *src_queue)
{
    link_srcQueue = src_queue;
}

void
NetworkLink::scheduleEvent(uint64_t time)
{
    m_net_ptr->getEventQueue()->schedule(this, time);
}

void
NetworkLink::wakeup()
{
    if (link_srcQueue->isReady(m_net_ptr->getEventQueue()->get_current_time())) {
        flit *t_flit = link_srcQueue->getTopFlit();
        t_flit->set_time(m_net_ptr->getEventQueue()->get_current_time() + m_latency);
        linkBuffer.insert(t_flit);
        link_consumer->scheduleEvent(m_latency);
        m_link_utilized++;
        m_vc_load[t_flit->get_vc()]++;
    }

    if (!link_srcQueue->isEmpty()) {
        m_net_ptr->getEventQueue()->schedule(this, 1);
    }
}

// The following methods are removed for the standalone version
// void
// NetworkLink::resetStats()
// {
//     for (int i = 0; i < m_vc_load.size(); i++) {
//         m_vc_load[i] = 0;
//     }

//     m_link_utilized = 0;
// }

// bool
// NetworkLink::functionalRead(Packet *pkt, WriteMask &mask)
// {
//     return linkBuffer.functionalRead(pkt, mask);
// }

// uint32_t
// NetworkLink::functionalWrite(Packet *pkt)
// {
//     return linkBuffer.functionalWrite(pkt);
// }

} // namespace garnet

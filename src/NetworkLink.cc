/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
 * All rights reserved.
 */

#include "NetworkLink.hh"
#include "GarnetNetwork.hh"
#include "Consumer.hh"
#include "CreditLink.hh"
#include "flitBuffer.hh"
#include "NetDest.hh"

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
}

NetworkLink::~NetworkLink()
{
    while (!linkBuffer.isEmpty()) {
        delete linkBuffer.getTopFlit();
    }
}

void NetworkLink::setLinkConsumer(Consumer *consumer) { link_consumer = consumer; }
void NetworkLink::setVcsPerVnet(uint32_t consumerVcs) { m_vc_load.resize(m_virt_nets * consumerVcs); }
void NetworkLink::setSourceQueue(flitBuffer *src_queue) { link_srcQueue = src_queue; }
void NetworkLink::scheduleEvent(uint64_t time) { m_net_ptr->getEventQueue()->schedule(this, time); }

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

} // namespace garnet
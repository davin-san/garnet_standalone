/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
 * All rights reserved.
 */

#include "NetworkInterface.hh"
#include <cassert>
#include <cmath>
#include <limits>

#include "Credit.hh"
#include "flitBuffer.hh"
#include "OutVcState.hh"
#include "GarnetNetwork.hh"
#include "SimpleTrafficGenerator.hh" 

namespace garnet
{

NetworkInterface::NetworkInterface(const Params &p)
  : m_id(p.id),
    m_virtual_networks(p.virtual_networks), m_vc_per_vnet(p.vcs_per_vnet),
    m_vc_allocator(m_virtual_networks, 0),
    m_deadlock_threshold(p.deadlock_threshold)
{
    m_net_ptr = p.net_ptr;
    m_stall_count.resize(m_virtual_networks);
    m_traffic_generator = nullptr; 
    m_vnet_to_vc_map.resize(m_virtual_networks, -1); 
}

NetworkInterface::~NetworkInterface()
{
    for (auto p : inPorts) delete p;
    for (auto p : outPorts) delete p;

    for (auto& buf : niOutVcs) {
        while (!buf.isEmpty()) {
            flit* fl = buf.getTopFlit();
            delete fl; // Flit destructor handles NetDest
        }
    }
}

void NetworkInterface::setTrafficGenerator(SimpleTrafficGenerator* tg) { m_traffic_generator = tg; }

void NetworkInterface::init() { /* NIC initialization if needed */ }

void NetworkInterface::addInPort(NetworkLink *in_link, CreditLink *credit_link)
{
    InputPort *newInPort = new InputPort(in_link, credit_link);
    inPorts.push_back(newInPort);
    in_link->setLinkConsumer(this);
    credit_link->setSourceQueue(newInPort->outCreditQueue());
    if (m_vc_per_vnet != 0) {
        in_link->setVcsPerVnet(m_vc_per_vnet);
        credit_link->setVcsPerVnet(m_vc_per_vnet);
    }
}

void NetworkInterface::addOutPort(NetworkLink *out_link, CreditLink *credit_link,
                             SwitchID router_id, uint32_t consumerVcs)
{
    OutputPort *newOutPort = new OutputPort(out_link, credit_link, router_id);
    outPorts.push_back(newOutPort);

    if (niOutVcs.size() == 0) { 
        m_vc_per_vnet = consumerVcs;
        int m_num_vcs = consumerVcs * m_virtual_networks;
        niOutVcs.resize(m_num_vcs); 
        outVcState.reserve(m_num_vcs);
        m_ni_out_vcs_enqueue_time.resize(m_num_vcs);
        for (int i = 0; i < m_num_vcs; i++) {
            m_ni_out_vcs_enqueue_time[i] = (uint64_t)-1; 
            outVcState.emplace_back(i, m_net_ptr, consumerVcs);
        }
        for (auto &iPort: inPorts) {
            iPort->inNetLink()->setVcsPerVnet(m_vc_per_vnet);
            credit_link->setVcsPerVnet(m_vc_per_vnet);
        }
    }

    out_link->setSourceQueue(newOutPort->outFlitQueue());
    out_link->setVcsPerVnet(m_vc_per_vnet);
    credit_link->setLinkConsumer(this);
    credit_link->setVcsPerVnet(m_vc_per_vnet);
}

void NetworkInterface::wakeup()
{
    assert(m_traffic_generator != nullptr);

    flit* ejected_flit = flit_eject();
    if (ejected_flit) {
        m_traffic_generator->receive_flit(ejected_flit);
    }

    flit* injected_flit = m_traffic_generator->send_flit();
    if (injected_flit) {
        bool success = flit_inj(injected_flit);
        if (!success) {
            m_traffic_generator->requeue_flit(injected_flit);
        }
    }

    uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();
    for (auto &oPort: outPorts) {
        CreditLink *inCreditLink = oPort->inCreditLink();
        if (inCreditLink->isReady(current_time)) {
            Credit *t_credit = (Credit*) inCreditLink->consumeLink();
            outVcState[t_credit->get_vc()].increment_credit();
            if (t_credit->is_free_signal()) {
                outVcState[t_credit->get_vc()].setState(IDLE_, current_time);
            }
            delete t_credit;
        }
    }

    scheduleOutputLink();

    for (auto &iPort: inPorts) {
        if (iPort->outCreditQueue()->getSize() > 0) {
            iPort->outCreditLink()->scheduleEvent(1);
        }
    }
}

int NetworkInterface::calculateVC(int vnet)
{
    for (int i = 0; i < m_vc_per_vnet; i++) {
        int delta = m_vc_allocator[vnet];
        m_vc_allocator[vnet]++;
        if (m_vc_allocator[vnet] == m_vc_per_vnet)
            m_vc_allocator[vnet] = 0;

        if (outVcState[(vnet*m_vc_per_vnet) + delta].isInState(
                    IDLE_, m_net_ptr->getEventQueue()->get_current_time())) {
            return ((vnet*m_vc_per_vnet) + delta);
        }
    }
    return -1;
}

bool NetworkInterface::flit_inj(flit* flt)
{
    int vnet = flt->get_vnet();
    int vc = m_vnet_to_vc_map[vnet];
    uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

    if (flt->get_type() == HEAD_ || flt->get_type() == HEAD_TAIL_) {
        assert(vc == -1);
        vc = calculateVC(vnet);
        if (vc == -1) return false;
        
        m_vnet_to_vc_map[vnet] = vc; 
        outVcState[vc].setState(ACTIVE_, current_time);
    }

    flt->set_vc(vc);
    niOutVcs[vc].insert(flt);
    m_ni_out_vcs_enqueue_time[vc] = current_time;

    if (flt->get_type() == TAIL_ || flt->get_type() == HEAD_TAIL_) {
        m_vnet_to_vc_map[vnet] = -1;
    }

    return true;
}

void NetworkInterface::scheduleOutputPort(OutputPort *oPort)
{
   int vc = oPort->vcRoundRobin();
   uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

   for (int i = 0; i < (int)niOutVcs.size(); i++) {
       vc = (vc + 1) % niOutVcs.size();

       int t_vnet = get_vnet(vc);
       if (oPort->isVnetSupported(t_vnet)) {
           if (niOutVcs[vc].isReady(current_time) && outVcState[vc].has_credit()) {
               oPort->vcRoundRobin(vc);
               outVcState[vc].decrement_credit();
               flit *t_flit = niOutVcs[vc].getTopFlit();
               t_flit->set_time(current_time);
               scheduleFlit(t_flit);

               if (t_flit->get_type() == TAIL_ || t_flit->get_type() == HEAD_TAIL_) {
                   m_ni_out_vcs_enqueue_time[vc] = (uint64_t)-1;
               }
               return;
           }
       }
   }
}

void NetworkInterface::scheduleOutputLink()
{
    for (auto &oPort: outPorts) {
        scheduleOutputPort(oPort);
    }
}

NetworkInterface::InputPort * NetworkInterface::getInportForVnet(int vnet) { for (auto &iPort : inPorts) if (iPort->isVnetSupported(vnet)) return iPort; return nullptr; }
NetworkInterface::OutputPort * NetworkInterface::getOutportForVnet(int vnet) { for (auto &oPort : outPorts) if (oPort->isVnetSupported(vnet)) return oPort; return nullptr; }

void NetworkInterface::scheduleFlit(flit *t_flit)
{
    OutputPort *oPort = getOutportForVnet(t_flit->get_vnet());
    if (oPort) {
        t_flit->set_enqueue_time(m_net_ptr->getEventQueue()->get_current_time());
        oPort->outFlitQueue()->insert(t_flit);
        oPort->outNetLink()->scheduleEvent(1);
        return;
    }
    delete t_flit;
}

int NetworkInterface::get_vnet(int vc) { for (int i = 0; i < m_virtual_networks; i++) if (vc >= (i*m_vc_per_vnet) && vc < ((i+1)*m_vc_per_vnet)) return i; return -1; }

flit* NetworkInterface::flit_eject()
{
    for (auto &iPort : inPorts) {
        NetworkLink* inNetLink = iPort->inNetLink();
        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();
        if (inNetLink->isReady(current_time)) {
            flit* flt = inNetLink->consumeLink();
            Credit* c = new Credit(flt->get_vc(),
                                 flt->get_type() == TAIL_ ||
                                 flt->get_type() == HEAD_TAIL_,
                                 current_time);
            iPort->sendCredit(c);
            iPort->outCreditLink()->scheduleEvent(1);
            return flt;
        }
    }
    return nullptr;
}

void NetworkInterface::print(std::ostream& out) const { out << "[NI]"; }
void NetworkInterface::scheduleEvent(uint64_t time) { m_net_ptr->getEventQueue()->schedule(this, time); }

NetworkInterface::OutputPort::OutputPort(NetworkLink *outLink, CreditLink *creditLink, int routerID)
{ _vnets = outLink->mVnets; _outFlitQueue = new flitBuffer(); _outNetLink = outLink; _inCreditLink = creditLink; _routerID = routerID; _bitWidth = outLink->bitWidth; _vcRoundRobin = 0; }
NetworkInterface::OutputPort::~OutputPort() { while(!_outFlitQueue->isEmpty()) delete _outFlitQueue->getTopFlit(); delete _outFlitQueue; }
NetworkInterface::InputPort::InputPort(NetworkLink *inLink, CreditLink *creditLink) { _vnets = inLink->mVnets; _outCreditQueue = new flitBuffer(); _inNetLink = inLink; _outCreditLink = creditLink; _bitWidth = inLink->bitWidth; }
NetworkInterface::InputPort::~InputPort() { while(!_outCreditQueue->isEmpty()) delete _outCreditQueue->getTopFlit(); delete _outCreditQueue; }

} // namespace garnet
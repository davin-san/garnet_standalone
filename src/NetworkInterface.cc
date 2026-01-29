/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
 * All rights reserved.
 *
 * ... (copyright header) ...
 */


#include "NetworkInterface.hh"

#include <cassert>
#include <cmath>

#include "Credit.hh"
#include "flitBuffer.hh"
#include "OutVcState.hh"
#include "GarnetNetwork.hh"
#include "SimpleTrafficGenerator.hh" // Include the new traffic generator

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
    
    m_traffic_generator = nullptr; // Initialize to nullptr

    // --- FIX: Initialize the vnet->vc map ---
    m_vnet_to_vc_map.resize(m_virtual_networks, -1); // -1 means no VC allocated
}

// --- ADD THIS ENTIRE FUNCTION ---
NetworkInterface::~NetworkInterface()
{
    for (auto p : inPorts) {
        delete p;
    }
    for (auto p : outPorts) {
        delete p;
    }

    // Clean up any flits left in NI buffers
    for (auto& buf : niOutVcs) {
        while (!buf.isEmpty()) {
            flit* fl = buf.getTopFlit();
            if (fl->get_type() == TAIL_ || fl->get_type() == HEAD_TAIL_) {
                 delete fl->get_route().net_dest;
            }
            delete fl;
        }
    }
}
// --- END OF ADDITION ---

NetworkInterface::OutputPort::OutputPort(NetworkLink *outLink, CreditLink *creditLink,
    int routerID)
{
    _vnets = outLink->mVnets;
    _outFlitQueue = new flitBuffer();

    _outNetLink = outLink;
    _inCreditLink = creditLink;

    _routerID = routerID;
    _bitWidth = outLink->bitWidth;
    _vcRoundRobin = 0;
}

// --- ADD THIS ENTIRE FUNCTION ---
NetworkInterface::OutputPort::~OutputPort()
{
    while(!_outFlitQueue->isEmpty()) {
       flit* fl = _outFlitQueue->getTopFlit();
       if (fl->get_type() == TAIL_ || fl->get_type() == HEAD_TAIL_) {
            delete fl->get_route().net_dest;
       }
       delete fl;
   }
   delete _outFlitQueue;
}
// --- END OF ADDITION ---

// --- ADD THIS ENTIRE FUNCTION ---
NetworkInterface::InputPort::~InputPort()
{
    while(!_outCreditQueue->isEmpty()) {
        delete _outCreditQueue->getTopFlit(); // These are Credits
    }
    delete _outCreditQueue;
}
// --- END OF ADDITION ---

NetworkInterface::InputPort::InputPort(NetworkLink *inLink, CreditLink *creditLink)
{
    _vnets = inLink->mVnets;
    _outCreditQueue = new flitBuffer();

    _inNetLink = inLink;
    _outCreditLink = creditLink;
    _bitWidth = inLink->bitWidth;
}

void
NetworkInterface::set_traffic_generator(SimpleTrafficGenerator* tg)
{
    m_traffic_generator = tg;
}

void
NetworkInterface::addInPort(NetworkLink *in_link,
                              CreditLink *credit_link)
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

void
NetworkInterface::addOutPort(NetworkLink *out_link,
                             CreditLink *credit_link,
                             SwitchID router_id, uint32_t consumerVcs)
{
    OutputPort *newOutPort = new OutputPort(out_link, credit_link, router_id);
    outPorts.push_back(newOutPort);

    assert(consumerVcs > 0);
    // We are not allowing different physical links to have different vcs
    // If it is required that the Network Interface support different VCs
    // for every physical link connected to it. Then they need to change
    // the logic within outport and inport.
    
    // This 'if' block will now correctly execute on the first call
    if (niOutVcs.size() == 0) { 
        m_vc_per_vnet = consumerVcs;
        int m_num_vcs = consumerVcs * m_virtual_networks;
        niOutVcs.resize(m_num_vcs); // Resize it here
        outVcState.reserve(m_num_vcs);
        m_ni_out_vcs_enqueue_time.resize(m_num_vcs);
        // instantiating the NI flit buffers
        for (int i = 0; i < m_num_vcs; i++) {
            m_ni_out_vcs_enqueue_time[i] = (uint64_t)-1; // ~0
            outVcState.emplace_back(i, m_net_ptr, consumerVcs);
        }

        // Reset VC Per VNET for input links already instantiated
        for (auto &iPort: inPorts) {
            NetworkLink *inNetLink = iPort->inNetLink();
            inNetLink->setVcsPerVnet(m_vc_per_vnet);
            credit_link->setVcsPerVnet(m_vc_per_vnet);
        }
    } else {
        //         fatal_if(consumerVcs != m_vc_per_vnet,
        // "%s: Connected Physical links have different vc requests: %d and %d\n",
        // name(), consumerVcs, m_vc_per_vnet);
    }

    out_link->setSourceQueue(newOutPort->outFlitQueue());
    out_link->setVcsPerVnet(m_vc_per_vnet);
    credit_link->setLinkConsumer(this);
    credit_link->setVcsPerVnet(m_vc_per_vnet);
}

// ... (other methods remain the same) ...

void
NetworkInterface::wakeup()
{
    assert(m_traffic_generator != nullptr); // Should be set by main

    // Eject flits from the network
    flit* ejected_flit = flit_eject();
    if (ejected_flit) {
        // Pass the flit to the traffic generator
        m_traffic_generator->receive_flit(ejected_flit);
    }

    // Inject flits into the network
    // This is now stateful (sends NULL if stalled)
    flit* injected_flit = m_traffic_generator->send_flit();
    if (injected_flit) {
        bool success = flit_inj(injected_flit);
        
        // --- FIX: Handle stall from flit_inj ---
        if (!success) {
            // Stall! The TG needs to hold this flit.
            m_traffic_generator->requeue_flit(injected_flit);
        }
    }

    scheduleOutputLink();

    /****************** Check the incoming credit link *******/
    uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

    for (auto &oPort: outPorts) {
        CreditLink *inCreditLink = oPort->inCreditLink();
        if (inCreditLink->isReady(current_time)) {
            Credit *t_credit = (Credit*) inCreditLink->consumeLink();
            outVcState[t_credit->get_vc()].increment_credit();
            if (t_credit->is_free_signal()) {
                outVcState[t_credit->get_vc()].setState(IDLE_,
                    current_time);
            }
            delete t_credit;
        }
    }

    for (auto &iPort: inPorts) {
        if (iPort->outCreditQueue()->getSize() > 0) {
            iPort->outCreditLink()->
                scheduleEvent(1);
        }
    }

    // --- FIX: Schedule self for next cycle if traffic generator is active ---
    // This ensures the traffic generator gets a chance to inject flits every cycle.
    if (m_traffic_generator) {
        scheduleEvent(1);
    }
}

// ... (calculateVC is the same) ...
int
NetworkInterface::calculateVC(int vnet)
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

// --- FIX: Rewritten flit_inj logic ---
bool
NetworkInterface::flit_inj(flit* flt)
{
    int vnet = flt->get_vnet();
    int vc = m_vnet_to_vc_map[vnet];
    uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

    if (flt->get_type() == HEAD_ || flt->get_type() == HEAD_TAIL_) {
        // This is a new packet. Find a new VC.
        assert(vc == -1); // Assert that we're not in the middle of a packet
        vc = calculateVC(vnet);

        if (vc == -1) {
            std::cout << "[Cycle " << current_time << "] NI " << m_id << " STALL: No VC available for packet " << flt->getPacketID() << std::endl;
            // No free VC for a new packet.
            // The TrafficGenerator must retry sending this flit.
            return false; // Stall
        }
        
        m_vnet_to_vc_map[vnet] = vc; // Store this VC for the packet
        outVcState[vc].setState(ACTIVE_, current_time);
    
    } else {
        // This is a BODY or TAIL flit.
        if (vc == -1) {
            // ERROR: Got a BODY/TAIL flit but no HEAD was sent
            // This is a logic error in the TG or NI
            assert(false);
        }
        // Use the stored VC
    }

    flt->set_vc(vc);
    niOutVcs[vc].insert(flt);
    m_ni_out_vcs_enqueue_time[vc] = current_time;

    std::cout << "[Cycle " << current_time << "] NI " << m_id << " injected flit " << flt->get_id() << " of packet " << flt->getPacketID() << " into VC " << vc << std::endl;

    if (flt->get_type() == TAIL_ || flt->get_type() == HEAD_TAIL_) {
        // This packet is done, free the vnet->vc mapping
        m_vnet_to_vc_map[vnet] = -1;
    }

    return true;
}

// ... (scheduleOutputPort and other methods are unchanged) ...

void
NetworkInterface::scheduleOutputPort(OutputPort *oPort)
{
   int vc = oPort->vcRoundRobin();
   uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();

   for (int i = 0; i < niOutVcs.size(); i++) {
       vc++;
       if (vc == niOutVcs.size())
           vc = 0;

       int t_vnet = get_vnet(vc);
       if (oPort->isVnetSupported(t_vnet)) {
           // model buffer backpressure
           if (niOutVcs[vc].isReady(current_time) &&
               outVcState[vc].has_credit()) {

               // Update the round robin arbiter
               oPort->vcRoundRobin(vc);

               outVcState[vc].decrement_credit();

               // Just removing the top flit
               flit *t_flit = niOutVcs[vc].getTopFlit();
               t_flit->set_time(current_time + 1);

               // Scheduling the flit
               scheduleFlit(t_flit);

               if (t_flit->get_type() == TAIL_ ||
                  t_flit->get_type() == HEAD_TAIL_) {
                   m_ni_out_vcs_enqueue_time[vc] = (uint64_t)-1; // ~0;
               }

               // Done with this port, continue to schedule
               // other ports
               return;
           }
       }
   }
}



/** This function looks at the NI buffers
 * if some buffer has flits which are ready to traverse the link in the next
 * cycle, and the downstream output vc associated with this flit has buffers
 * left, the link is scheduled for the next cycle
 */

void
NetworkInterface::scheduleOutputLink()
{
    // Schedule each output link
    for (auto &oPort: outPorts) {
        scheduleOutputPort(oPort);
    }
}

NetworkInterface::InputPort *
NetworkInterface::getInportForVnet(int vnet)
{
    for (auto &iPort : inPorts) {
        if (iPort->isVnetSupported(vnet)) {
            return iPort;
        }
    }

    return nullptr;
}

/*
 * This function returns the outport which supports the given vnet.
 * Currently, HeteroGarnet does not support multiple outports to
 * support same vnet. Thus, this function returns the first-and
 * only outport which supports the vnet.
 */
NetworkInterface::OutputPort *
NetworkInterface::getOutportForVnet(int vnet)
{
    for (auto &oPort : outPorts) {
        if (oPort->isVnetSupported(vnet)) {
            return oPort;
        }
    }

    return nullptr;
}
void
NetworkInterface::scheduleFlit(flit *t_flit)
{
    OutputPort *oPort = getOutportForVnet(t_flit->get_vnet());

    if (oPort) {
        oPort->outFlitQueue()->insert(t_flit);
        oPort->outNetLink()->scheduleEvent(1);
        return;
    }

    // If no outport is found, we must delete the flit
    // or it will be leaked. This happens if the topology
    // is misconfigured and the traffic generator
    // sends a flit to a vnet this NI doesn't support.
    
    // --- FIX: Also delete the NetDest ---
    if (t_flit->get_type() == TAIL_ || t_flit->get_type() == HEAD_TAIL_) {
        delete t_flit->get_route().net_dest;
    }
    delete t_flit;
}

int
NetworkInterface::get_vnet(int vc)
{
    for (int i = 0; i < m_virtual_networks; i++) {
        if (vc >= (i*m_vc_per_vnet) && vc < ((i+1)*m_vc_per_vnet)) {
            return i;
        }
    }
    return -1;
}

flit*
NetworkInterface::flit_eject()
{
    // Check all input ports
    for (auto &iPort : inPorts) {
        NetworkLink* inNetLink = iPort->inNetLink();
        uint64_t current_time = m_net_ptr->getEventQueue()->get_current_time();
        if (inNetLink->isReady(current_time)) {
            flit* flt = inNetLink->consumeLink();

            std::cout << "[Cycle " << current_time << "] NI " << m_id << " ejected flit " << flt->get_id() << " of packet " << flt->getPacketID() << std::endl;

            // Send credit back to router
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

// The following methods are removed for the standalone version
// void NetworkInterface::checkReschedule() {}

void
NetworkInterface::print(std::ostream& out) const
{
    out << "[Network Interface]";
}

void
NetworkInterface::scheduleEvent(uint64_t time)
{
    // Schedule a wakeup event for this NI
    m_net_ptr->getEventQueue()->schedule(this, time);
}

// The following methods are removed for the standalone version
// bool NetworkInterface::functionalRead(Packet *pkt, WriteMask &mask) { return false; }
// uint32_t NetworkInterface::functionalWrite(Packet *pkt) { return 0; }

} // namespace garnet
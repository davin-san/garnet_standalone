/*
 * Copyright (c) 2020 Advanced Micro Devices, Inc.
 * Copyright (c) 2020 Inria
 * Copyright (c) 2016 Georgia Institute of Technology
 * Copyright (c) 2008 Princeton University
 * All rights reserved.
 *
 * ... (copyright header) ...
 */


#ifndef __GARNET_NETWORK_INTERFACE_HH__
#define __GARNET_NETWORK_INTERFACE_HH__

#include <iostream>
#include <vector>
#include <sstream>
#include <deque>

#include "CommonTypes.hh"
#include "Consumer.hh"
#include "flit.hh"
#include "NetworkLink.hh"
#include "CreditLink.hh"
#include "flitBuffer.hh"
#include "OutVcState.hh"

namespace garnet
{

// Forward declaration for traffic generator
class SimpleTrafficGenerator;

struct GarnetNetworkInterfaceParams {
    int id;
    int x, y, z;
    int virtual_networks;
    int vcs_per_vnet;
    int deadlock_threshold;
    GarnetNetwork *net_ptr;
};

class NetworkInterface : public Consumer
{
  public:
    typedef GarnetNetworkInterfaceParams Params;
    NetworkInterface(const Params &p);
    ~NetworkInterface();

    void addInPort(NetworkLink *in_link, CreditLink *credit_link);
    void addOutPort(NetworkLink *out_link, CreditLink *credit_link,
                    SwitchID router_id, uint32_t consumerVcs);

    void init(); // Added init method
    void wakeup();

    // New interface for traffic generators
    bool flit_inj(flit *flt);
    flit *flit_eject();

    // Method to attach the traffic generator
    void setTrafficGenerator(SimpleTrafficGenerator *tg);

    void print(std::ostream &out) const;
    int get_vnet(int vc);
    NodeID get_id() const { return m_id; }

    int get_x() const { return m_x; }
    int get_y() const { return m_y; }
    int get_z() const { return m_z; }

    void scheduleEvent(uint64_t time) override;

    void scheduleFlit(flit *t_flit);

    int get_router_id(int vnet)
    {
        OutputPort *oPort = getOutportForVnet(vnet);
        if (oPort)
            return oPort->routerID();
        return -1;
    }

  private:
    int calculateVC(int vnet);

    class OutputPort
    {
      public:
          OutputPort(NetworkLink *outLink, CreditLink *creditLink,
              int routerID);;
          ~OutputPort(); // <-- ADD THIS LINE

          flitBuffer *
          outFlitQueue()
          {
              return _outFlitQueue;
          }

          NetworkLink *
          outNetLink()
          {
              return _outNetLink;
          }

          CreditLink *
          inCreditLink()
          {
              return _inCreditLink;
          }

          int
          routerID()
          {
              return _routerID;
          }

          uint32_t bitWidth()
          {
              return _bitWidth;
          }

          bool isVnetSupported(int pVnet)
          {
              if (!_vnets.size()) {
                  return true;
              }

              for (auto &it : _vnets) {
                  if (it == pVnet) {
                      return true;
                  }
              }
              return false;

          }

          std::string
          printVnets()
          {
              std::stringstream ss;
              for (auto &it : _vnets) {
                  ss << it;
                  ss << " ";
              }
              return ss.str();
          }

          int vcRoundRobin()
          {
              return _vcRoundRobin;
          }

          void vcRoundRobin(int vc)
          {
              _vcRoundRobin = vc;
          }


      private:
          std::vector<int> _vnets;
          flitBuffer *_outFlitQueue;

          NetworkLink *_outNetLink;
          CreditLink *_inCreditLink;

          int _vcRoundRobin; // For round robin scheduling

          int _routerID;
          uint32_t _bitWidth;
    };

    class InputPort
    {
      public:
          InputPort(NetworkLink *inLink, CreditLink *creditLink);;
          ~InputPort(); // <-- ADD THIS LINE

          flitBuffer *
          outCreditQueue()
          {
              return _outCreditQueue;
          }

          NetworkLink *
          inNetLink()
          {
              return _inNetLink;
          }

          CreditLink *
          outCreditLink()
          {
              return _outCreditLink;
          }

          bool isVnetSupported(int pVnet)
          {
              if (!_vnets.size()) {
                  return true;
              }

              for (auto &it : _vnets) {
                  if (it == pVnet) {
                      return true;
                  }
              }
              return false;

          }

          void sendCredit(Credit *cFlit)
          {
              _outCreditQueue->insert(cFlit);
          }

          uint32_t bitWidth()
          {
              return _bitWidth;
          }

          std::string
          printVnets()
          {
              std::stringstream ss;
              for (auto &it : _vnets) {
                  ss << it;
                  ss << " ";
              }
              return ss.str();
          }

          // Queue for stalled flits
          std::deque<flit *> m_stall_queue;
          bool messageEnqueuedThisCycle;
      private:
          std::vector<int> _vnets;
          flitBuffer *_outCreditQueue;

          NetworkLink *_inNetLink;
          CreditLink *_outCreditLink;
          uint32_t _bitWidth;
    };


  private:
    GarnetNetwork *m_net_ptr;
    const NodeID m_id;
    int m_x, m_y, m_z;
    const int m_virtual_networks;
    int m_vc_per_vnet;
    std::vector<int> m_vc_allocator;
    std::vector<OutputPort *> outPorts;
    std::vector<InputPort *> inPorts;
    int m_deadlock_threshold;
    std::vector<OutVcState> outVcState;

    std::vector<int> m_stall_count;

    // Input Flit Buffers
    std::vector<flitBuffer>  niOutVcs;
    std::vector<uint64_t> m_ni_out_vcs_enqueue_time;

    // --- FIX: Add state to track VC assignment per vnet ---
    std::vector<int> m_vnet_to_vc_map;

    // Pointer to the traffic generator
    SimpleTrafficGenerator* m_traffic_generator;

    void checkStallQueue();

    void scheduleOutputPort(OutputPort *oPort);
    void scheduleOutputLink();
    void checkReschedule();

    InputPort *getInportForVnet(int vnet);
    OutputPort *getOutportForVnet(int vnet);
};

} // namespace garnet

#endif // __GARNET_NETWORK_INTERFACE_HH__
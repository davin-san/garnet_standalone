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


#ifndef __GARNET_NETWORK_LINK_HH__
#define __GARNET_NETWORK_LINK_HH__

#include <iostream>
#include <vector>
#include <cstdint>

#include "CommonTypes.hh"
#include "flitBuffer.hh"
#include "GarnetSimObject.hh"

namespace garnet
{

class GarnetNetwork;
class Consumer;

struct NetworkLinkParams {
    int id;
    uint64_t latency;
    int virtual_networks;
    GarnetNetwork* net_ptr;
};

class NetworkLink : public GarnetSimObject
{
  public:
    typedef NetworkLinkParams Params;
    NetworkLink(const Params &p);
    ~NetworkLink();

    void setLinkConsumer(Consumer *consumer);
    void setSourceQueue(flitBuffer *src_queue);
    void scheduleEvent(uint64_t time);
    virtual void setVcsPerVnet(uint32_t consumerVcs);
    void setType(link_type type) { m_type = type; }
    link_type getType() { return m_type; }
    void print(std::ostream& out) const {}
    int get_id() const { return m_id; }
    flitBuffer *getBuffer() { return &linkBuffer;}
    virtual void wakeup();

    unsigned int getLinkUtilization() const { return m_link_utilized; }
    const std::vector<unsigned int> & getVcLoad() const { return m_vc_load; }

    inline bool isReady(uint64_t curTime)
    {
        return linkBuffer.isReady(curTime);
    }

    inline flit* peekLink() { return linkBuffer.peekTopFlit(); }
    inline flit* consumeLink() { return linkBuffer.getTopFlit(); }

    // The following methods are removed for the standalone version
    // bool functionalRead(Packet *pkt, WriteMask &mask);
    // uint32_t functionalWrite(Packet *);
    // void resetStats();

    std::vector<int> mVnets;
    uint32_t bitWidth;

  private:
    const int m_id;
    link_type m_type;
    const uint64_t m_latency;

    // Statistical variables
    unsigned int m_link_utilized;
    std::vector<unsigned int> m_vc_load;

  protected:
    GarnetNetwork* m_net_ptr;
    uint32_t m_virt_nets;
    flitBuffer linkBuffer;
    Consumer *link_consumer;
    flitBuffer *link_srcQueue;

};

} // namespace garnet

#endif // __GARNET_NETWORK_LINK_HH__

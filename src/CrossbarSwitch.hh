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


#ifndef __GARNET_CROSSBAR_SWITCH_HH__
#define __GARNET_CROSSBAR_SWITCH_HH__

#include <iostream>
#include <vector>

#include "CommonTypes.hh"
#include "flitBuffer.hh"
#include "GarnetSimObject.hh"

namespace garnet
{

class Router;

class CrossbarSwitch : public GarnetSimObject
{
  public:
    CrossbarSwitch(Router *router);
    ~CrossbarSwitch();
    void wakeup();
    void init();
    void print(std::ostream& out) const {};

    inline void
    update_sw_winner(int inport, flit *t_flit)
    {
        switchBuffers[inport].insert(t_flit);
    }

    // The following methods are removed for the standalone version
    // inline double get_crossbar_activity() { return 0; }
    // bool functionalRead(Packet *pkt, WriteMask &mask);
    // uint32_t functionalWrite(Packet *pkt);
    // void resetStats();

  private:
    Router *m_router;
    int m_num_vcs;
    std::vector<flitBuffer> switchBuffers;
};

} // namespace garnet

#endif // __GARNET_CROSSBAR_SWITCH_HH__

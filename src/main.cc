/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
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

#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <getopt.h>
#include <string>

#include "GarnetNetwork.hh"
#include "Topology.hh"
#include "EventQueue.hh"

using namespace garnet;

struct SimConfig {
    int num_rows = 2;
    int num_cols = 2;
    int sim_cycles = 1000;
    double injection_rate = 0.01;
    int routing_algorithm = 1; // 1 = XY, 0 = Table
    int packet_size = 1; // 1 = Single Flit
    std::string topology = "Mesh_XY";
    bool deterministic_test = false;
};

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  --rows <int>        Number of rows (default: 2)\n"
              << "  --cols <int>        Number of columns (default: 2)\n"
              << "  --cycles <int>      Simulation cycles (default: 1000)\n"
              << "  --rate <float>      Injection rate (0.0-1.0, default: 0.01)\n"
              << "  --packet-size <int> Packet size in flits (default: 1)\n"
              << "  --routing <int>     0=Table, 1=XY (default: 1)\n"
              << "  --test-mode         Enable deterministic test (Packet 0->3)\n"
              << "  --help              Show this message\n";
}

void parse_args(int argc, char** argv, SimConfig& config) {
    struct option long_options[] = {
        {"rows", required_argument, 0, 'r'},
        {"cols", required_argument, 0, 'c'},
        {"cycles", required_argument, 0, 'n'},
        {"rate", required_argument, 0, 'i'},
        {"packet-size", required_argument, 0, 'p'},
        {"routing", required_argument, 0, 'a'},
        {"test-mode", no_argument, 0, 't'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:c:n:i:p:a:th", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'r': config.num_rows = std::atoi(optarg); break;
            case 'c': config.num_cols = std::atoi(optarg); break;
            case 'n': config.sim_cycles = std::atoi(optarg); break;
            case 'i': config.injection_rate = std::atof(optarg); break;
            case 'p': config.packet_size = std::atoi(optarg); break;
            case 'a': config.routing_algorithm = std::atoi(optarg); break;
            case 't': config.deterministic_test = true; break;
            case 'h': print_usage(argv[0]); exit(0);
            default: print_usage(argv[0]); exit(1);
        }
    }
}

int main(int argc, char** argv) {
    SimConfig config;
    parse_args(argc, argv, config);

    std::cout << "Garnet Standalone Simulation" << std::endl;
    std::cout << "Topology: " << config.topology 
              << " (" << config.num_rows << "x" << config.num_cols << ")" << std::endl;
    std::cout << "Routing: " << (config.routing_algorithm == 1 ? "XY" : "Table") << std::endl;
    std::cout << "Cycles: " << config.sim_cycles << std::endl;

    srand(time(NULL));

    // 1. Define Network Params
    GarnetNetwork::Params net_params;
    net_params.num_rows = config.num_rows;
    net_params.num_cols = config.num_cols;
    net_params.ni_flit_size = 16;
    net_params.vcs_per_vnet = 4;
    net_params.buffers_per_data_vc = 4;
    net_params.buffers_per_ctrl_vc = 1;
    net_params.routing_algorithm = config.routing_algorithm;
    net_params.enable_fault_model = false;

    // 2. Instantiate Network
    GarnetNetwork network(net_params);
    network.init();

    // 3. Build Topology
    Topology* topo = Topology::create(config.topology, &network, 
                                      config.num_rows, config.num_cols);
    topo->build();

    // 4. Configure Traffic Generators
    for (auto tg : topo->getTGs()) {
        tg->set_packet_size(config.packet_size);
        if (config.deterministic_test) {
            tg->set_active(true); // Enable test mode
            tg->set_injection_rate(0.0); // Disable random injection
        } else {
            tg->set_active(false); // Disable test mode
            tg->set_injection_rate(config.injection_rate);
        }
    }
    
    std::cout << "Initializing routers..." << std::endl;
    for (auto router : topo->getRouters()) {
        router->init();
    }

    std::cout << "Starting simulation..." << std::endl;

    EventQueue* event_queue = network.getEventQueue();

    // Schedule initial events
    for (auto ni : topo->getNIs()) {
        ni->scheduleEvent(1);
    }

    // Run Loop
    while (!event_queue->is_empty() && event_queue->get_current_time() <= config.sim_cycles) {
        Event* event = event_queue->get_next_event();
        event->get_obj()->wakeup();
        delete event;
    }

    std::cout << "\nSimulation finished." << std::endl;
    std::cout << "  - Total Cycles Simulated: " << config.sim_cycles << std::endl;
    std::cout << "  - Final Event Queue Time: " << network.getEventQueue()->get_current_time() << std::endl;

    // Cleanup
    delete topo; // Topology owns the components now
    
    return 0;
}

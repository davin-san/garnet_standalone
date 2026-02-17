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
#include "NetworkLink.hh"

using namespace garnet;

struct SimConfig {
    int num_rows = 2;
    int num_cols = 2;
    int num_depth = 1;
    int sim_cycles = 1000;
    double injection_rate = 0.01;
    int routing_algorithm = 1; // 1 = XY, 0 = Table
    int packet_size = 1; 
    std::string topology = "Mesh_XY";
    bool deterministic_test = false;
    bool debug = false;
    bool trace_packet = false;
    bool enable_fault_model = false;
    int seed = 42;
};

void parse_args(int argc, char** argv, SimConfig& config) {
    struct option long_options[] = {
        {"topology", required_argument, 0, 'T'},
        {"rows", required_argument, 0, 'r'},
        {"cols", required_argument, 0, 'c'},
        {"depth", required_argument, 0, 'z'},
        {"cycles", required_argument, 0, 'n'},
        {"rate", required_argument, 0, 'i'},
        {"packet-size", required_argument, 0, 'p'},
        {"routing", required_argument, 0, 'a'},
        {"test-mode", no_argument, 0, 't'},
        {"debug", no_argument, 0, 'd'},
        {"trace-packet", no_argument, 0, 'x'},
        {"fault-model", no_argument, 0, 'f'},
        {"seed", required_argument, 0, 's'},
        {0, 0, 0, 0}
    };
    int opt;
    while ((opt = getopt_long(argc, argv, "T:r:c:z:n:i:p:a:tdxfs:", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'T': config.topology = optarg; break;
            case 'r': config.num_rows = std::atoi(optarg); break;
            case 'c': config.num_cols = std::atoi(optarg); break;
            case 'z': config.num_depth = std::atoi(optarg); break;
            case 'n': config.sim_cycles = std::atoi(optarg); break;
            case 'i': config.injection_rate = std::atof(optarg); break;
            case 'p': config.packet_size = std::atoi(optarg); break;
            case 'a': config.routing_algorithm = std::atoi(optarg); break;
            case 't': config.deterministic_test = true; break;
            case 'd': config.debug = true; break;
            case 'x': config.trace_packet = true; break;
            case 'f': config.enable_fault_model = true; break;
            case 's': config.seed = std::atoi(optarg); break;
        }
    }
}

int main(int argc, char** argv) {
    SimConfig config;
    parse_args(argc, argv, config);

    GarnetNetwork::Params net_params;
    net_params.num_rows = config.num_rows;
    net_params.num_cols = config.num_cols;
    net_params.num_depth = config.num_depth;
    net_params.ni_flit_size = 16;
    net_params.vcs_per_vnet = 4;
    net_params.buffers_per_data_vc = 4;
    net_params.buffers_per_ctrl_vc = 1;
    net_params.routing_algorithm = config.routing_algorithm;
    net_params.enable_fault_model = config.enable_fault_model;
    net_params.enable_debug = config.debug;

    GarnetNetwork network(net_params);

    Topology* topo = Topology::create(config.topology, &network, config.num_rows, config.num_cols, config.num_depth);
    topo->build();

    network.init();

    for (auto tg : topo->getTGs()) {
        tg->set_packet_size(config.packet_size);
        tg->set_seed(config.seed);
        tg->set_trace_packet(config.trace_packet);
        if (config.deterministic_test) {
            tg->set_active(true);
            tg->set_injection_rate(0.0);
        } else {
            tg->set_active(false);
            tg->set_injection_rate(config.injection_rate);
        }
    }
    
    for (auto router : topo->getRouters()) router->init();

    EventQueue* event_queue = network.getEventQueue();
    for (uint64_t t = 0; t <= (uint64_t)config.sim_cycles; t++) {
        event_queue->set_current_time(t);
        for (auto ni : topo->getNIs()) {
            ni->wakeup();
        }
        for (auto router : topo->getRouters()) {
            router->wakeup();
        }
        while (!event_queue->is_empty() && event_queue->peek_next_time() <= t) {
            Event* event = event_queue->get_next_event();
            event->get_obj()->wakeup();
            delete event;
        }
    }

    std::cout << "\nSimulation Statistics:" << std::endl;
    std::cout << "  - Total Cycles: " << config.sim_cycles << std::endl;

    uint64_t total_latency = 0, total_packets = 0, total_injected = 0;
    uint64_t vnet_pkts[2] = {0, 0}, vnet_lat[2] = {0, 0};

    for (auto tg : topo->getTGs()) {
        total_latency += tg->get_total_latency();
        total_packets += tg->get_received_packets();
        total_injected += tg->get_injected_packets();
        vnet_pkts[0] += tg->get_received_vnet(0);
        vnet_lat[0] += tg->get_latency_vnet(0);
        vnet_pkts[1] += tg->get_received_vnet(1);
        vnet_lat[1] += tg->get_latency_vnet(1);
    }

    std::cout << "  - Packets Injected: " << total_injected << std::endl;
    std::cout << "  - Total Packets Received: " << total_packets << std::endl;
    if (total_packets > 0) {
        std::cout << "  - Average Network Latency: " << (double)total_latency / total_packets << " cycles" << std::endl;
        for(int v=0; v<2; v++) {
            if (vnet_pkts[v] > 0)
                std::cout << "    - VNet " << v << ": Rx=" << vnet_pkts[v] << ", Lat=" << (double)vnet_lat[v]/vnet_pkts[v] << std::endl;
        }
    }

    // Link Utilization
    double total_util = 0;
    int num_links = topo->getLinks().size();
    if (num_links > 0) {
        for (auto link : topo->getLinks()) {
            total_util += (double)link->getLinkUtilization() / config.sim_cycles;
        }
        std::cout << "  - Average Link Utilization: " << (total_util / num_links) * 100.0 << " %" << std::endl;
    }

    std::cout << "Simulation finished." << std::endl;

    delete topo;
    return 0;
}

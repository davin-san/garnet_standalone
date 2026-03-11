#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <getopt.h>
#include <string>
#include <sstream>
#include <map>

#include "GarnetNetwork.hh"
#include "Topology.hh"
#include "EventQueue.hh"
#include "NetworkLink.hh"
#include "PaceAdapter.hh"

using namespace garnet;

// ---- Helper: parse comma-separated integer list ----
static std::vector<int> parse_int_list(const std::string& s) {
    std::vector<int> result;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) result.push_back(std::atoi(tok.c_str()));
    }
    return result;
}

struct SimConfig {
    int num_rows = 2;
    int num_cols = 2;
    int num_depth = 1;
    int num_cpus = 0;           // 0 = derive from rows*cols or chiplet params
    int sim_cycles = 1000;
    double injection_rate = 0.01;
    int routing_algorithm = 1;  // 1=XY, 0=TABLE
    int packet_size = 1;
    int vcs_per_vnet = 4;
    std::string topology = "Mesh_XY";
    std::string synthetic = "";  // "pace" or "uniform_random" or ""
    bool deterministic_test = false;
    bool debug = false;
    bool trace_packet = false;
    bool enable_fault_model = false;
    int seed = 42;

    // PACE mode
    std::string pace_profile = "";
    int         pace_mshr_limit = 16;
    std::string pace_output = "pace_results.json";
    std::string pace_dir_routers = ""; // comma-separated gateway router IDs

    // Chiplet topology params
    int num_chiplets = 4;
    int intra_rows = 4;
    int intra_cols = 4;
    std::string inter_topology = "ring";
    int inter_latency = 1;
    int inter_width = 128;

    // PACE ablation flags
    bool pace_no_per_source    = false;
    bool pace_no_phases        = false;
    bool pace_no_mshr          = false;
    bool pace_no_remap         = false;
    bool pace_no_weighted_dest = false;
    bool pace_no_corr_response = false;
};

void parse_args(int argc, char** argv, SimConfig& config) {
    struct option long_options[] = {
        // Standard flags (existing)
        {"topology",          required_argument, 0, 'T'},
        {"rows",              required_argument, 0, 'r'},
        {"cols",              required_argument, 0, 'c'},
        {"depth",             required_argument, 0, 'z'},
        {"cycles",            required_argument, 0, 'n'},
        {"rate",              required_argument, 0, 'i'},
        {"packet-size",       required_argument, 0, 'p'},
        {"routing",           required_argument, 0, 'a'},
        {"test-mode",         no_argument,       0, 't'},
        {"debug",             no_argument,       0, 'd'},
        {"trace-packet",      no_argument,       0, 'x'},
        {"fault-model",       no_argument,       0, 'f'},
        {"seed",              required_argument, 0, 's'},
        // PACE-specific (existing)
        {"pace-profile",      required_argument, 0, 'P'},
        {"pace-mshr",         required_argument, 0, 'M'},
        {"pace-output",       required_argument, 0, 'O'},
        // Gem5-compatible new flags
        {"network",           required_argument, 0, 2000}, // accepted, ignored
        {"num-cpus",          required_argument, 0, 2001},
        {"vcs-per-vnet",      required_argument, 0, 2002},
        {"synthetic",         required_argument, 0, 2003},
        {"injectionrate",     required_argument, 0, 2004},
        // Chiplet topology flags
        {"num-chiplets",      required_argument, 0, 2010},
        {"intra-rows",        required_argument, 0, 2011},
        {"intra-cols",        required_argument, 0, 2012},
        {"inter-topology",    required_argument, 0, 2013},
        {"inter-latency",     required_argument, 0, 2014},
        {"inter-width",       required_argument, 0, 2015},
        {"pace-dir-routers",  required_argument, 0, 2016},
        // PACE ablation flags (existing)
        {"pace-no-per-source",    no_argument,   0, 1001},
        {"pace-no-phases",        no_argument,   0, 1002},
        {"pace-no-mshr",          no_argument,   0, 1003},
        {"pace-no-remap",         no_argument,   0, 1004},
        {"pace-no-weighted-dest", no_argument,   0, 1005},
        {"pace-no-corr-response", no_argument,   0, 1006},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv,
                              "T:r:c:z:n:i:p:a:tdxfs:P:M:O:",
                              long_options, nullptr)) != -1) {
        switch (opt) {
            case 'T': config.topology         = optarg; break;
            case 'r': config.num_rows          = std::atoi(optarg); break;
            case 'c': config.num_cols          = std::atoi(optarg); break;
            case 'z': config.num_depth         = std::atoi(optarg); break;
            case 'n': config.sim_cycles        = std::atoi(optarg); break;
            case 'i': config.injection_rate    = std::atof(optarg); break;
            case 'p': config.packet_size       = std::atoi(optarg); break;
            case 'a': config.routing_algorithm = std::atoi(optarg); break;
            case 't': config.deterministic_test = true; break;
            case 'd': config.debug             = true; break;
            case 'x': config.trace_packet      = true; break;
            case 'f': config.enable_fault_model = true; break;
            case 's': config.seed              = std::atoi(optarg); break;
            case 'P': config.pace_profile      = optarg; break;
            case 'M': config.pace_mshr_limit   = std::atoi(optarg); break;
            case 'O': config.pace_output       = optarg; break;

            // Gem5-compatible flags
            case 2000: /* --network=garnet: ignored */ break;
            case 2001: config.num_cpus         = std::atoi(optarg); break;
            case 2002: config.vcs_per_vnet     = std::atoi(optarg); break;
            case 2003: config.synthetic        = optarg; break;
            case 2004: config.injection_rate   = std::atof(optarg); break;

            // Chiplet topology flags
            case 2010: config.num_chiplets     = std::atoi(optarg); break;
            case 2011: config.intra_rows       = std::atoi(optarg); break;
            case 2012: config.intra_cols       = std::atoi(optarg); break;
            case 2013: config.inter_topology   = optarg; break;
            case 2014: config.inter_latency    = std::atoi(optarg); break;
            case 2015: config.inter_width      = std::atoi(optarg); break;
            case 2016: config.pace_dir_routers = optarg; break;

            // Ablation flags
            case 1001: config.pace_no_per_source    = true; break;
            case 1002: config.pace_no_phases        = true; break;
            case 1003: config.pace_no_mshr          = true; break;
            case 1004: config.pace_no_remap         = true; break;
            case 1005: config.pace_no_weighted_dest = true; break;
            case 1006: config.pace_no_corr_response = true; break;
        }
    }
}

// ---- Standard (non-PACE) simulation ----
static void run_standard(const SimConfig& config, Topology* topo,
                          GarnetNetwork& network)
{
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
    for (uint64_t t = 0; t <= (uint64_t)config.sim_cycles; ++t) {
        event_queue->set_current_time(t);
        for (auto ni : topo->getNIs())         ni->wakeup();
        for (auto router : topo->getRouters()) router->wakeup();
        while (!event_queue->is_empty() &&
               event_queue->peek_next_time() <= t) {
            Event* ev = event_queue->get_next_event();
            ev->get_obj()->wakeup();
            delete ev;
        }
    }

    std::cout << "\nSimulation Statistics:\n"
              << "  - Total Cycles: " << config.sim_cycles << "\n";

    uint64_t total_latency = 0, total_packets = 0, total_injected = 0;
    uint64_t vnet_pkts[2] = {0, 0}, vnet_lat[2] = {0, 0};

    for (auto tg : topo->getTGs()) {
        total_latency  += tg->get_total_latency();
        total_packets  += tg->get_received_packets();
        total_injected += tg->get_injected_packets();
        vnet_pkts[0]   += tg->get_received_vnet(0);
        vnet_lat[0]    += tg->get_latency_vnet(0);
        vnet_pkts[1]   += tg->get_received_vnet(1);
        vnet_lat[1]    += tg->get_latency_vnet(1);
    }

    std::cout << "  - Packets Injected: " << total_injected << "\n"
              << "  - Total Packets Received: " << total_packets << "\n";
    if (total_packets > 0) {
        std::cout << "  - Average Network Latency: "
                  << (double)total_latency / total_packets << " cycles\n";
        for (int v = 0; v < 2; ++v) {
            if (vnet_pkts[v] > 0)
                std::cout << "    - VNet " << v << ": Rx=" << vnet_pkts[v]
                          << ", Lat=" << (double)vnet_lat[v] / vnet_pkts[v]
                          << "\n";
        }
    }

    double total_util = 0;
    int num_links = (int)topo->getLinks().size();
    if (num_links > 0) {
        for (auto link : topo->getLinks())
            total_util += (double)link->getLinkUtilization() / config.sim_cycles;
        std::cout << "  - Average Link Utilization: "
                  << (total_util / num_links) * 100.0 << " %\n";
    }

    std::cout << "Simulation finished.\n";
}

// ---- PACE simulation ----
static void run_pace(const SimConfig& config, Topology* topo,
                     GarnetNetwork& network)
{
    PaceAdapter::AblationConfig ablation;
    ablation.no_per_source    = config.pace_no_per_source;
    ablation.no_phases        = config.pace_no_phases;
    ablation.no_mshr          = config.pace_no_mshr;
    ablation.no_remap         = config.pace_no_remap;
    ablation.no_weighted_dest = config.pace_no_weighted_dest;
    ablation.no_corr_response = config.pace_no_corr_response;

    PaceAdapter adapter(config.pace_profile, config.pace_mshr_limit, config.seed,
                        ablation);

    // Override directory remapping if --pace-dir-routers was given.
    // The list "0,16,32,48" maps dir 0->router 0, dir 1->router 16, etc.
    if (!config.pace_dir_routers.empty()) {
        std::vector<int> ids = parse_int_list(config.pace_dir_routers);
        std::map<int,int> remap;
        for (int d = 0; d < (int)ids.size(); ++d)
            remap[d] = ids[d];
        adapter.set_directory_remapping(remap);
    }

    adapter.init(topo->getNIs(), &network);

    for (auto tg : adapter.getTGs())
        tg->set_trace_packet(config.trace_packet);

    for (auto router : topo->getRouters()) router->init();

    uint64_t total_cycles = adapter.total_network_cycles();
    std::cout << "PACE: total simulation length = " << total_cycles << " cycles\n";

    EventQueue* event_queue = network.getEventQueue();
    uint64_t t = 0;
    for (; t <= total_cycles; ++t) {
        event_queue->set_current_time(t);
        if (t > 0 && !adapter.tick(t)) break;
        for (auto ni : topo->getNIs())         ni->wakeup();
        for (auto router : topo->getRouters()) router->wakeup();
        while (!event_queue->is_empty() &&
               event_queue->peek_next_time() <= t) {
            Event* ev = event_queue->get_next_event();
            ev->get_obj()->wakeup();
            delete ev;
        }
    }

    // Drain window
    uint64_t drain_cycles = 200;
    for (uint64_t d = 0; d < drain_cycles; ++d, ++t) {
        event_queue->set_current_time(t);
        for (auto ni : topo->getNIs())         ni->wakeup();
        for (auto router : topo->getRouters()) router->wakeup();
        while (!event_queue->is_empty() &&
               event_queue->peek_next_time() <= t) {
            Event* ev = event_queue->get_next_event();
            ev->get_obj()->wakeup();
            delete ev;
        }
    }

    std::cout << "\nPACE Simulation Statistics:\n"
              << "  - Total Cycles: " << t << "\n";

    uint64_t total_latency = 0, total_packets = 0, total_injected = 0;
    uint64_t vnet_pkts[3] = {0, 0, 0}, vnet_lat[3] = {0, 0, 0};
    int max_mshr = 0;

    for (auto tg : adapter.getTGs()) {
        total_latency  += tg->get_total_latency();
        total_packets  += tg->get_received_packets();
        total_injected += tg->get_injected_packets();
        for (int v = 0; v < 3; ++v) {
            vnet_pkts[v] += tg->get_received_vnet(v);
            vnet_lat[v]  += tg->get_latency_vnet(v);
        }
        if (tg->get_max_mshr_count() > max_mshr)
            max_mshr = tg->get_max_mshr_count();
    }

    std::cout << "  - Packets Injected: " << total_injected << "\n"
              << "  - Total Packets Received: " << total_packets << "\n";
    if (total_packets > 0) {
        std::cout << "  - Average Packet Latency: "
                  << (double)total_latency / total_packets << " cycles\n";
        for (int v = 0; v < 3; ++v) {
            if (vnet_pkts[v] > 0)
                std::cout << "    - VNet " << v << ": Rx=" << vnet_pkts[v]
                          << ", Lat=" << (double)vnet_lat[v] / vnet_pkts[v]
                          << "\n";
        }
    }
    std::cout << "  - Peak MSHR Occupancy (any node): " << max_mshr
              << " / " << config.pace_mshr_limit << "\n";

    double total_util = 0;
    int num_links = (int)topo->getLinks().size();
    if (num_links > 0) {
        for (auto link : topo->getLinks())
            total_util += (double)link->getLinkUtilization() / (double)t;
        std::cout << "  - Average Link Utilization: "
                  << (total_util / num_links) * 100.0 << " %\n";
    }

    adapter.dump_results(config.pace_output, topo->getLinks(), t);
    std::cout << "PACE simulation finished.\n";
}

int main(int argc, char** argv) {
    SimConfig config;
    parse_args(argc, argv, config);

    // Detect PACE mode: --synthetic=pace OR legacy (pace_profile non-empty without --synthetic)
    bool pace_mode = !config.pace_profile.empty() &&
                     (config.synthetic.empty() || config.synthetic == "pace");

    // Chiplet topologies require TABLE routing (algorithm=0).
    bool is_chiplet = (config.topology == "PACE_Chiplet" ||
                       config.topology == "PACE_Chiplet_CMesh");
    if (is_chiplet) config.routing_algorithm = 0;

    GarnetNetwork::Params net_params;
    net_params.num_rows          = config.num_rows;
    net_params.num_cols          = config.num_cols;
    net_params.num_depth         = config.num_depth;
    net_params.ni_flit_size      = 16;
    net_params.vcs_per_vnet      = config.vcs_per_vnet;
    net_params.buffers_per_data_vc = 4;
    net_params.buffers_per_ctrl_vc = 1;
    net_params.routing_algorithm = config.routing_algorithm;
    net_params.enable_fault_model = config.enable_fault_model;
    net_params.enable_debug      = config.debug;

    GarnetNetwork network(net_params);

    // Build topology-creation params.
    TopologyParams tparams;
    tparams.num_chiplets   = config.num_chiplets;
    tparams.intra_rows     = config.intra_rows;
    tparams.intra_cols     = config.intra_cols;
    tparams.inter_topology = config.inter_topology;
    tparams.inter_latency  = config.inter_latency;
    tparams.inter_width    = config.inter_width;
    tparams.vcs_per_vnet   = config.vcs_per_vnet;

    Topology* topo = Topology::create(config.topology, &network,
                                      config.num_rows, config.num_cols,
                                      config.num_depth, tparams);

    // PACE mode needs 3 vnets (request / response / writeback).
    if (pace_mode) topo->set_num_vnets(3);
    topo->set_vcs_per_vnet(config.vcs_per_vnet);

    topo->build();
    network.init();

    if (pace_mode) {
        run_pace(config, topo, network);
    } else {
        run_standard(config, topo, network);
    }

    delete topo;
    return 0;
}

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <getopt.h>
#include <string>
#include <sstream>
#include <map>
#include <cmath>

#include "GarnetNetwork.hh"
#include "Topology.hh"
#include "EventQueue.hh"
#include "NetworkLink.hh"
#include "PaceAdapter.hh"
#include "PaceProfile.hh"
#include "StandaloneStats.hh"
#include "SimpleTrafficGenerator.hh"

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
    int         pace_packets_per_node = 100;
    double      pace_temporal_floor = 2.0;

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
    bool pace_no_burst         = false;  // --burst-model=off

    // Sweep mode: --sweep-lambda-range=start:end:step  (multipliers)
    std::string sweep_lambda_range = "";

    // New fields (spec-compliant)
    std::string topo_id = "";
    bool        uniform_mode = false;  // --uniform: profile-aware uniform injection
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
        {"pace-packets-per-node", required_argument, 0, 2017},
        {"pace-temporal-floor",   required_argument, 0, 2018},
        {"pace-target-packets",   required_argument, 0, 2019}, // alias
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
        // Burst model and sweep mode (existing)
        {"burst-model",           required_argument, 0, 1007},
        {"sweep-lambda-range",    required_argument, 0, 1008},
        // ── Spec-compliant aliases (new) ──
        {"profile",               required_argument, 0, 3000}, // alias --pace-profile
        {"mshr-limit",            required_argument, 0, 3001}, // alias --pace-mshr
        {"output",                required_argument, 0, 3002}, // alias --pace-output
        {"sim-cycles",            required_argument, 0, 3003}, // alias --cycles
        {"topo-id",               required_argument, 0, 3004}, // new: target topology ID
        {"uniform",               no_argument,       0, 3005}, // new: uniform injection mode
        {"link-width",            required_argument, 0, 3006}, // alias --inter-width
        // Spec-compliant ablation aliases
        {"uniform-injection",     no_argument,   0, 3007}, // alias --pace-no-per-source
        {"single-phase",          no_argument,   0, 3008}, // alias --pace-no-phases
        {"no-dir-remap",          no_argument,   0, 3009}, // alias --pace-no-remap
        {"uniform-destinations",  no_argument,   0, 3010}, // alias --pace-no-weighted-dest
        {"no-correlated-responses", no_argument, 0, 3011}, // alias --pace-no-corr-response
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
            case 2017: config.pace_packets_per_node = std::atoi(optarg); break;
            case 2018: config.pace_temporal_floor = std::atof(optarg); break;
            case 2019: config.pace_packets_per_node = std::atoi(optarg); break;

            // Ablation flags (existing names)
            case 1001: config.pace_no_per_source    = true; break;
            case 1002: config.pace_no_phases        = true; break;
            case 1003: config.pace_no_mshr          = true; break;
            case 1004: config.pace_no_remap         = true; break;
            case 1005: config.pace_no_weighted_dest = true; break;
            case 1006: config.pace_no_corr_response = true; break;
            case 1007: // --burst-model=on|off
                if (std::string(optarg) == "off")
                    config.pace_no_burst = true;
                break;
            case 1008: config.sweep_lambda_range = optarg; break;

            // Spec-compliant aliases (new)
            case 3000: config.pace_profile      = optarg; break;
            case 3001: config.pace_mshr_limit   = std::atoi(optarg); break;
            case 3002: config.pace_output       = optarg; break;
            case 3003: config.sim_cycles        = std::atoi(optarg); break;
            case 3004: config.topo_id           = optarg; break;
            case 3005: config.uniform_mode      = true; break;
            case 3006: config.inter_width       = std::atoi(optarg); break;
            // Spec ablation aliases
            case 3007: config.pace_no_per_source    = true; break;
            case 3008: config.pace_no_phases        = true; break;
            case 3009: config.pace_no_remap         = true; break;
            case 3010: config.pace_no_weighted_dest = true; break;
            case 3011: config.pace_no_corr_response = true; break;
        }
    }
}

// ---- Helper: parse "start:end:step" sweep range into a list of multipliers ----
static std::vector<double> parse_sweep_range(const std::string& s) {
    std::vector<double> result;
    std::string t = s;
    std::vector<double> parts;
    std::istringstream ss(t);
    std::string tok;
    while (std::getline(ss, tok, ':')) {
        if (!tok.empty()) parts.push_back(std::atof(tok.c_str()));
    }
    if (parts.size() == 3) {
        double start = parts[0], end = parts[1], step = parts[2];
        if (step <= 0) step = 0.1;
        for (double v = start; v <= end + step * 0.01; v += step)
            result.push_back(v);
    } else if (parts.size() == 1) {
        result.push_back(parts[0]);
    } else {
        std::istringstream ss2(s);
        double v;
        while (ss2 >> v) result.push_back(v);
    }
    return result;
}

// ---- Helper: write a latency histogram JSON block ----
static void write_hist_json(std::ofstream& f, const LatHist& h) {
    f << "  \"latency_histogram\": {\n"
      << "    \"fine_counts\": [";
    for (size_t i = 0; i < h.fine.size(); ++i) { if (i > 0) f << ", "; f << h.fine[i]; }
    f << "],\n    \"coarse_counts\": [";
    for (size_t i = 0; i < h.coarse.size(); ++i) { if (i > 0) f << ", "; f << h.coarse[i]; }
    f << "],\n    \"ultra_counts\": [";
    for (size_t i = 0; i < h.ultra.size(); ++i) { if (i > 0) f << ", "; f << h.ultra[i]; }
    f << "],\n    \"overflow_count\": " << h.overflow << "\n  }";
}

// ---- Helper: write a link_utilization JSON block ----
static void write_link_util_json(std::ofstream& f,
                                  const std::vector<NetworkLink*>& links,
                                  uint64_t total_cycles) {
    struct LI { int idx; double util; };
    std::vector<LI> lu;
    double sum = 0.0, mx = 0.0;
    for (int i = 0; i < (int)links.size(); ++i) {
        double u = total_cycles > 0
                   ? (double)links[i]->getLinkUtilization() / total_cycles : 0.0;
        lu.push_back({i, u});
        sum += u;
        if (u > mx) mx = u;
    }
    std::sort(lu.begin(), lu.end(), [](const LI& a, const LI& b){return a.util > b.util;});
    double avg = lu.empty() ? 0.0 : sum / lu.size();
    f << "  \"link_utilization\": {\n"
      << "    \"max_link_util\": " << mx << ",\n"
      << "    \"avg_link_util\": " << avg << ",\n"
      << "    \"top5_links\": [";
    int n5 = std::min((int)lu.size(), 5);
    for (int i = 0; i < n5; ++i) {
        if (i > 0) f << ", ";
        f << "{\"link_idx\": " << lu[i].idx
          << ", \"utilization\": " << lu[i].util << "}";
    }
    f << "]\n  }";
}

// ---- Write full spec B.2 JSON for uniform mode ----
static void write_uniform_json(const std::string& path,
                                const LatHist& hist,
                                uint64_t total_packets,
                                uint64_t total_flits,
                                uint64_t total_latency,
                                uint64_t total_injected,
                                uint64_t total_cycles,
                                const std::vector<NetworkLink*>& links,
                                const std::string& method,
                                const std::string& topo_id,
                                int inter_latency,
                                int inter_width,
                                const std::string& benchmark,
                                double lambda_multiplier) {
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "Uniform: cannot write results to " << path << "\n";
        return;
    }

    double avg_lat = total_packets > 0 ? (double)total_latency / total_packets : 0.0;
    double p50  = hist.percentile(0.50);
    double p99  = hist.percentile(0.99);
    double p999 = hist.percentile(0.999);
    double max_lat = hist.max_latency();

    double throughput   = total_cycles > 0 ? (double)total_flits / total_cycles : 0.0;
    double offered_load = total_cycles > 0 ? (double)total_injected / total_cycles : 0.0;

    std::string topo_cfg = topo_id.empty() ? ""
        : topo_id + "_lat" + std::to_string(inter_latency) + "_w" + std::to_string(inter_width);

    f << std::fixed << std::setprecision(6);
    f << "{\n"
      << "  \"method\": \""          << method    << "\",\n"
      << "  \"benchmark\": \""       << benchmark << "\",\n"
      << "  \"topo_id\": \""         << topo_id   << "\",\n"
      << "  \"inter_latency\": "     << inter_latency << ",\n"
      << "  \"inter_width\": "       << inter_width   << ",\n"
      << "  \"topo_config\": \""     << topo_cfg      << "\",\n"
      << "  \"avg_packet_latency\": "   << avg_lat    << ",\n"
      << "  \"avg_flit_latency\": "     << avg_lat    << ",\n"
      << "  \"p50_latency\": "          << p50        << ",\n"
      << "  \"p99_latency\": "          << p99        << ",\n"
      << "  \"p999_latency\": "         << p999       << ",\n"
      << "  \"max_latency\": "          << max_lat    << ",\n"
      << "  \"throughput_flits_per_cycle\": "    << throughput   << ",\n"
      << "  \"offered_load_flits_per_cycle\": "  << offered_load << ",\n"
      << "  \"accepted_load_flits_per_cycle\": " << throughput   << ",\n"
      << "  \"injection_blocked_pct\": 0.0,\n"
      << "  \"mshr_saturated\": false,\n"
      << "  \"raw_total_packets\": "  << total_packets  << ",\n"
      << "  \"raw_total_flits\": "    << total_flits    << ",\n"
      << "  \"simulated_cycles\": "   << total_cycles   << ",\n"
      << "  \"lambda_multiplier\": "  << lambda_multiplier << ",\n";

    write_hist_json(f, hist);
    f << ",\n";
    write_link_util_json(f, links, total_cycles);
    f << ",\n"
      << "  \"saturation_detail\": {\n"
      << "    \"max_avg_mshr_occupancy\": 0.0,\n"
      << "    \"avg_mshr_occupancy_all_nodes\": 0.0,\n"
      << "    \"saturated_nodes\": [],\n"
      << "    \"pct_cycles_mshr_full\": 0.0\n"
      << "  },\n"
      << "  \"buffer_pressure\": {\n"
      << "    \"max_avg_vc_occupancy\": 0.0,\n"
      << "    \"pct_vc_cycles_above_80\": 0.0\n"
      << "  }\n"
      << "}\n";
    f.close();

    std::cout << "Uniform: results written to " << path << "\n"
              << "  method=" << method
              << "  avg_lat=" << avg_lat
              << "  p99=" << p99
              << "  throughput=" << throughput << " flits/cycle\n";
}

// ---- Run standard (non-PACE, non-uniform-profile) simulation ----
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

    // Write pace_results.json for uniform mode if --pace-output is set.
    if (!config.pace_output.empty()) {
        double avg_lat = total_packets > 0
                         ? (double)total_latency / total_packets : 0.0;
        uint64_t total_flits = total_packets * (uint64_t)config.packet_size;
        double throughput = config.sim_cycles > 0
                            ? (double)total_flits / config.sim_cycles : 0.0;

        std::ofstream jf(config.pace_output);
        if (jf.is_open()) {
            jf << "{\n"
               << "  \"method\": \"uniform\",\n"
               << "  \"avg_packet_latency\": " << avg_lat << ",\n"
               << "  \"p99_latency\": 0,\n"
               << "  \"throughput_flits_per_cycle\": " << throughput << ",\n"
               << "  \"raw_total_packets\": " << total_packets << ",\n"
               << "  \"raw_total_flits\": " << total_flits << ",\n"
               << "  \"simulated_cycles\": " << config.sim_cycles << ",\n"
               << "  \"packet_stats\": {\n"
               << "    \"total_packets_received\": " << total_packets << ",\n"
               << "    \"total_flits_received\": " << total_flits << ",\n"
               << "    \"avg_latency_cycles\": " << avg_lat << ",\n"
               << "    \"p99_latency_cycles\": 0,\n"
               << "    \"throughput_flits_per_cycle\": " << throughput << "\n"
               << "  },\n"
               << "  \"saturation\": {\n"
               << "    \"near_saturation\": false,\n"
               << "    \"max_avg_mshr_occupancy\": 0,\n"
               << "    \"saturated_nodes\": []\n"
               << "  }\n"
               << "}\n";
            jf.close();
        }
    }
}

// ---- Profile-aware uniform simulation (full stats output) ----
// Called when --uniform and --profile are both given.
static void run_uniform(const SimConfig& config, Topology* topo,
                        GarnetNetwork& network)
{
    PaceProfile profile = PaceProfile::load(config.pace_profile);

    double effective_lambda = profile.effective_lambda;
    if (effective_lambda <= 0.0) {
        std::cerr << "Uniform: profile effective_lambda=" << effective_lambda
                  << " — defaulting to --rate value " << config.injection_rate << "\n";
        effective_lambda = config.injection_rate;
    }

    // Weighted-average flits_per_packet across phases
    double fpp_sum = 0.0; int64_t pkt_sum = 0;
    for (const auto& ph : profile.phases) {
        fpp_sum += ph.flits_per_packet * ph.total_packets;
        pkt_sum += ph.total_packets;
    }
    double fpp = (pkt_sum > 0) ? fpp_sum / pkt_sum : 1.0;
    int packet_size = std::max(1, (int)std::round(fpp));

    int num_nis = (int)topo->getNIs().size();

    std::cout << "Uniform: lambda=" << effective_lambda
              << "  packet_size=" << packet_size
              << "  sim_cycles=" << config.sim_cycles
              << "  num_nis=" << num_nis << "\n";

    for (auto tg : topo->getTGs()) {
        tg->set_packet_size(packet_size);
        tg->set_seed(config.seed);
        tg->set_trace_packet(config.trace_packet);
        tg->set_active(false);
        tg->set_injection_rate(effective_lambda);
    }

    for (auto router : topo->getRouters()) router->init();

    EventQueue* event_queue = network.getEventQueue();
    uint64_t t = 0;
    for (; t <= (uint64_t)config.sim_cycles; ++t) {
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

    // Collect merged statistics from all TGs
    LatHist merged_hist;
    uint64_t total_latency = 0, total_packets = 0, total_injected = 0;
    for (auto tg : topo->getTGs()) {
        merged_hist.merge(tg->get_lat_hist());
        total_latency  += tg->get_total_latency();
        total_packets  += tg->get_received_packets();
        total_injected += tg->get_injected_packets();
    }
    uint64_t total_flits = total_packets * (uint64_t)packet_size;

    std::string benchmark = profile.benchmark.empty() ? "unknown" : profile.benchmark;
    std::string topo_id   = config.topo_id.empty() ? profile.topo_id : config.topo_id;

    write_uniform_json(config.pace_output, merged_hist,
                       total_packets, total_flits, total_latency, total_injected,
                       t, topo->getLinks(),
                       "uniform", topo_id, config.inter_latency, config.inter_width,
                       benchmark, 1.0);
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
    ablation.no_burst         = config.pace_no_burst;

    PaceAdapter adapter(config.pace_profile, config.pace_mshr_limit, config.seed,
                        ablation, config.pace_packets_per_node,
                        config.pace_temporal_floor);

    adapter.set_topo_id(config.topo_id);
    adapter.set_inter_config(config.inter_latency, config.inter_width);

    if (!config.pace_dir_routers.empty()) {
        std::vector<int> ids = parse_int_list(config.pace_dir_routers);
        std::map<int,int> remap;
        for (int d = 0; d < (int)ids.size(); ++d)
            remap[d] = ids[d];
        adapter.set_directory_remapping(remap);
    }

    adapter.init(topo->getNIs(), &network, topo->get_diameter());

    for (auto tg : adapter.getTGs())
        tg->set_trace_packet(config.trace_packet);

    for (auto router : topo->getRouters()) router->init();

    EventQueue* event_queue = network.getEventQueue();
    uint64_t t = 0;

    for (; t < 1000000000; ++t) {
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

    adapter.dump_results(config.pace_output, topo->getLinks(), t);
    std::cout << "PACE simulation finished.\n";
}

// ---- Sweep mode: run PACE simulation for each lambda multiplier ----
static void run_sweep(const SimConfig& config, Topology* topo,
                      GarnetNetwork& network,
                      const std::vector<double>& multipliers)
{
    PaceAdapter::AblationConfig ablation;
    ablation.no_per_source    = config.pace_no_per_source;
    ablation.no_phases        = config.pace_no_phases;
    ablation.no_mshr          = config.pace_no_mshr;
    ablation.no_remap         = config.pace_no_remap;
    ablation.no_weighted_dest = config.pace_no_weighted_dest;
    ablation.no_corr_response = config.pace_no_corr_response;
    ablation.no_burst         = config.pace_no_burst;

    // Peek at profile for metadata to embed in combined sweep file
    PaceProfile peek = PaceProfile::load(config.pace_profile);
    std::string benchmark = peek.benchmark.empty() ? "unknown" : peek.benchmark;
    std::string topo_id   = config.topo_id.empty() ? peek.topo_id : config.topo_id;

    std::string base = config.pace_output;
    if (base.size() > 5 && base.substr(base.size()-5) == ".json")
        base = base.substr(0, base.size()-5);

    std::vector<std::string> point_files;

    for (int mi = 0; mi < (int)multipliers.size(); ++mi) {
        double mult = multipliers[mi];
        std::cout << "\n=== SWEEP point " << mi+1 << "/" << multipliers.size()
                  << "  lambda_mult=" << mult << " ===\n";

        PaceAdapter adapter(config.pace_profile, config.pace_mshr_limit,
                            config.seed + mi, ablation,
                            config.pace_packets_per_node,
                            config.pace_temporal_floor);
        adapter.scale_lambda(mult);
        adapter.set_topo_id(config.topo_id);
        adapter.set_inter_config(config.inter_latency, config.inter_width);

        if (!config.pace_dir_routers.empty()) {
            std::vector<int> ids = parse_int_list(config.pace_dir_routers);
            std::map<int,int> remap;
            for (int d = 0; d < (int)ids.size(); ++d) remap[d] = ids[d];
            adapter.set_directory_remapping(remap);
        }

        adapter.init(topo->getNIs(), &network, topo->get_diameter());

        for (auto tg : adapter.getTGs())
            tg->set_trace_packet(config.trace_packet);
        for (auto router : topo->getRouters()) router->init();

        EventQueue* event_queue = network.getEventQueue();
        uint64_t t = 0;
        for (; t < 1000000000; ++t) {
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
        uint64_t drain = 200 + (uint64_t)(10 * topo->get_diameter());
        for (uint64_t d = 0; d < drain; ++d, ++t) {
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

        std::ostringstream pt_path;
        pt_path << base << "_sweep_" << std::fixed << std::setprecision(2) << mult << ".json";
        std::string pt = pt_path.str();
        adapter.dump_results_with_multiplier(pt, topo->getLinks(), t, mult);
        point_files.push_back(pt);
    }

    // Write combined sweep JSON
    std::string sweep_path = base + "_sweep.json";
    std::ofstream sf(sweep_path);
    if (sf.is_open()) {
        sf << "{\n"
           << "  \"benchmark\": \"" << benchmark << "\",\n"
           << "  \"topo_id\": \"" << topo_id << "\",\n"
           << "  \"method\": \"pace\",\n"
           << "  \"sweep_type\": \"lambda\",\n"
           << "  \"base_lambda\": " << peek.effective_lambda << ",\n"
           << "  \"sweep_results\": [\n";
        for (int mi = 0; mi < (int)point_files.size(); ++mi) {
            std::ifstream pf(point_files[mi]);
            if (pf.is_open()) {
                std::string content((std::istreambuf_iterator<char>(pf)),
                                     std::istreambuf_iterator<char>());
                size_t s = content.find('{');
                size_t e = content.rfind('}');
                if (s != std::string::npos && e != std::string::npos)
                    content = content.substr(s, e - s + 1);
                sf << "    " << content;
                if (mi + 1 < (int)point_files.size()) sf << ",";
                sf << "\n";
            }
        }
        sf << "  ]\n}\n";
        sf.close();
        std::cout << "PACE sweep: combined results written to " << sweep_path << "\n";
    }
}

// ---- Profile-aware uniform sweep ----
static void run_uniform_sweep(const SimConfig& config, Topology* topo,
                               GarnetNetwork& network,
                               const std::vector<double>& multipliers)
{
    PaceProfile profile = PaceProfile::load(config.pace_profile);

    double base_lambda = profile.effective_lambda;
    if (base_lambda <= 0.0) base_lambda = config.injection_rate;

    double fpp_sum = 0.0; int64_t pkt_sum = 0;
    for (const auto& ph : profile.phases) {
        fpp_sum += ph.flits_per_packet * ph.total_packets;
        pkt_sum += ph.total_packets;
    }
    double fpp = (pkt_sum > 0) ? fpp_sum / pkt_sum : 1.0;
    int packet_size = std::max(1, (int)std::round(fpp));

    int num_nis = (int)topo->getNIs().size();
    std::string benchmark = profile.benchmark.empty() ? "unknown" : profile.benchmark;
    std::string topo_id   = config.topo_id.empty() ? profile.topo_id : config.topo_id;

    std::string base_out = config.pace_output;
    if (base_out.size() > 5 && base_out.substr(base_out.size()-5) == ".json")
        base_out = base_out.substr(0, base_out.size()-5);

    std::vector<std::string> point_files;

    for (int mi = 0; mi < (int)multipliers.size(); ++mi) {
        double mult   = multipliers[mi];
        double lambda = base_lambda * mult;
        std::cout << "\n=== UNIFORM SWEEP point " << mi+1 << "/" << multipliers.size()
                  << "  lambda_mult=" << mult << "  lambda=" << lambda << " ===\n";

        // Create fresh TGs for this sweep point (clean histograms per point).
        std::vector<SimpleTrafficGenerator*> sweep_tgs;
        for (int i = 0; i < num_nis; ++i) {
            NetworkInterface* ni = topo->getNIs()[i];
            auto* tg = new SimpleTrafficGenerator(i, num_nis, lambda, &network, ni);
            tg->set_packet_size(packet_size);
            tg->set_seed(config.seed + mi * 100 + i);
            tg->set_active(false);
            ni->setTrafficGenerator(tg);
            sweep_tgs.push_back(tg);
        }

        for (auto router : topo->getRouters()) router->init();

        EventQueue* event_queue = network.getEventQueue();
        uint64_t t = 0;
        for (; t <= (uint64_t)config.sim_cycles; ++t) {
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

        LatHist merged;
        uint64_t tot_lat = 0, tot_pkt = 0, tot_inj = 0;
        for (auto* tg : sweep_tgs) {
            merged.merge(tg->get_lat_hist());
            tot_lat += tg->get_total_latency();
            tot_pkt += tg->get_received_packets();
            tot_inj += tg->get_injected_packets();
        }
        uint64_t tot_flt = tot_pkt * (uint64_t)packet_size;
        uint64_t cycles_this = t;

        std::ostringstream pt_path;
        pt_path << base_out << "_sweep_" << std::fixed << std::setprecision(2) << mult << ".json";
        std::string pt = pt_path.str();
        write_uniform_json(pt, merged, tot_pkt, tot_flt, tot_lat, tot_inj,
                           cycles_this, topo->getLinks(),
                           "uniform", topo_id, config.inter_latency, config.inter_width,
                           benchmark, mult);
        point_files.push_back(pt);
        for (auto* tg : sweep_tgs) delete tg;
    }

    // Write combined sweep JSON
    std::string sweep_path = base_out + "_sweep.json";
    std::ofstream sf(sweep_path);
    if (sf.is_open()) {
        sf << "{\n"
           << "  \"benchmark\": \"" << benchmark << "\",\n"
           << "  \"topo_id\": \"" << topo_id << "\",\n"
           << "  \"method\": \"uniform\",\n"
           << "  \"sweep_type\": \"lambda\",\n"
           << "  \"base_lambda\": " << base_lambda << ",\n"
           << "  \"sweep_results\": [\n";
        for (int mi = 0; mi < (int)point_files.size(); ++mi) {
            std::ifstream pf(point_files[mi]);
            if (pf.is_open()) {
                std::string content((std::istreambuf_iterator<char>(pf)),
                                     std::istreambuf_iterator<char>());
                size_t s = content.find('{');
                size_t e = content.rfind('}');
                if (s != std::string::npos && e != std::string::npos)
                    content = content.substr(s, e - s + 1);
                sf << "    " << content;
                if (mi + 1 < (int)point_files.size()) sf << ",";
                sf << "\n";
            }
        }
        sf << "  ]\n}\n";
        sf.close();
        std::cout << "Uniform sweep: combined results written to " << sweep_path << "\n";
    }
}

int main(int argc, char** argv) {
    SimConfig config;
    parse_args(argc, argv, config);

    // PACE mode: profile given and not forced to uniform.
    bool pace_mode = !config.pace_profile.empty() && !config.uniform_mode &&
                     (config.synthetic.empty() || config.synthetic == "pace");

    // Profile-aware uniform: --uniform with a profile file.
    bool uniform_with_profile = config.uniform_mode && !config.pace_profile.empty();

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

    TopologyParams tparams;
    tparams.num_chiplets   = config.num_chiplets;
    tparams.intra_rows     = config.intra_rows;
    tparams.intra_cols     = config.intra_cols;
    tparams.inter_topology = config.inter_topology;
    tparams.inter_latency  = config.inter_latency;
    tparams.inter_width    = config.inter_width;
    tparams.vcs_per_vnet   = config.vcs_per_vnet;
    tparams.num_cpus       = config.num_cpus;

    Topology* topo = Topology::create(config.topology, &network,
                                      config.num_rows, config.num_cols,
                                      config.num_depth, tparams);

    // PACE mode needs 3 vnets; uniform with profile also uses 3 for compatibility.
    if (pace_mode || uniform_with_profile) topo->set_num_vnets(3);
    topo->set_vcs_per_vnet(config.vcs_per_vnet);

    topo->build();
    network.init();

    std::vector<double> multipliers;
    if (!config.sweep_lambda_range.empty()) {
        multipliers = parse_sweep_range(config.sweep_lambda_range);
        if (multipliers.empty()) {
            std::cerr << "ERROR: --sweep-lambda-range produced no points: "
                      << config.sweep_lambda_range << "\n";
            delete topo;
            return 1;
        }
    }

    if (pace_mode && !multipliers.empty()) {
        std::cout << "PACE sweep mode: " << multipliers.size() << " lambda multipliers\n";
        run_sweep(config, topo, network, multipliers);
    } else if (pace_mode) {
        run_pace(config, topo, network);
    } else if (uniform_with_profile && !multipliers.empty()) {
        std::cout << "Uniform sweep mode: " << multipliers.size() << " lambda multipliers\n";
        run_uniform_sweep(config, topo, network, multipliers);
    } else if (uniform_with_profile) {
        run_uniform(config, topo, network);
    } else {
        run_standard(config, topo, network);
    }

    delete topo;
    return 0;
}

// PACE Adapter + PACE Traffic Generator — full implementations.

#include "PaceAdapter.hh"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <sstream>

namespace garnet {

// ============================================================
// PaceTrafficGenerator — implementation
// ============================================================

PaceTrafficGenerator::PaceTrafficGenerator(int id, GarnetNetwork* net,
                                           NetworkInterface* ni,
                                           PaceAdapter* adapter,
                                           bool is_core, bool is_directory,
                                           int mshr_limit, int seed)
    : m_id(id), m_net_ptr(net), m_ni(ni), m_adapter(adapter),
      m_is_core(is_core), m_is_directory(is_directory),
      m_mshr_limit(mshr_limit), m_mshr_count(0), m_max_mshr_count(0),
      m_stalled_flit(nullptr),
      m_last_injection_cycle(static_cast<uint64_t>(-1)),
      m_total_latency(0), m_received_packets(0),
      m_injected_packets(0), m_injection_attempts(0),
      m_trace(false),
      m_dist(0.0, 1.0)
{
    m_rng.seed(seed + id);
    m_received_per_vnet.resize(3, 0);
    m_latency_per_vnet.resize(3, 0);
}

PaceTrafficGenerator::~PaceTrafficGenerator()
{
    if (m_stalled_flit) delete m_stalled_flit;
    while (!m_flit_queue.empty()) {
        delete m_flit_queue.front();
        m_flit_queue.pop();
    }
    // ResponseJobs hold no heap resources.
}

uint64_t PaceTrafficGenerator::current_time() const
{
    return m_net_ptr->getEventQueue()->get_current_time();
}

// Build and enqueue all flits of a packet into m_flit_queue.
void PaceTrafficGenerator::generate_packet(int dest_ni, int dest_router,
                                           int vnet, int num_flits,
                                           uint64_t time)
{
    int packet_id = m_net_ptr->getNextPacketID();
    uint32_t flit_width = m_net_ptr->getNiFlitSize();

    RouteInfo route;
    route.src_ni     = m_id;
    route.src_router = m_ni->get_router_id(vnet);
    route.dest_ni    = dest_ni;
    route.dest_router = dest_router;
    route.vnet       = vnet;
    route.net_dest.add(dest_ni);

    for (int i = 0; i < num_flits; ++i) {
        flit* fl = new flit(packet_id, i, 0, vnet, route,
                            num_flits, nullptr, 0, flit_width, time);
        fl->set_trace(m_trace);
        m_flit_queue.push(fl);
    }
}

flit* PaceTrafficGenerator::send_flit()
{
    uint64_t t = current_time();

    // (A) Return previously stalled flit (already generated, just retrying).
    if (m_stalled_flit) {
        flit* fl = m_stalled_flit;
        m_stalled_flit = nullptr;
        return fl;
    }

    // (B) Continue sending flits of the current in-progress packet.
    if (!m_flit_queue.empty()) {
        flit* fl = m_flit_queue.front();
        m_flit_queue.pop();
        fl->set_enqueue_time(t);
        return fl;
    }

    // (C) No current packet.  Only decide once per cycle.
    if (t == m_last_injection_cycle) return nullptr;
    m_last_injection_cycle = t;
    ++m_injection_attempts;

    // Priority 1: pending responses (directory role).
    if (!m_pending_responses.empty()) {
        ResponseJob job = m_pending_responses.front();
        m_pending_responses.pop();
        generate_packet(job.dest_ni, job.dest_router, /*vnet=*/1,
                        job.num_flits, t);
        ++m_injected_packets;
        flit* fl = m_flit_queue.front();
        m_flit_queue.pop();
        fl->set_enqueue_time(t);
        return fl;
    }

    // Priority 2: new request (core role only).
    // Directory nodes (m_id >= num_cpus, m_is_core=false) must NOT inject
    // new request traffic — they only generate responses via Priority 1.
    // This correctly handles profiles where per_router_injection has non-zero
    // entries for directory routers (e.g., from crossbar_activity counting
    // forwarded coherence messages in the extraction mesh).
    if (!m_is_core) return nullptr;

    // No new injection after all phases are exhausted (drain window).
    if (m_adapter->is_done()) return nullptr;

    // Hard MSHR cap (skipped when --pace-no-mshr is active).
    if (!m_adapter->no_mshr() && m_mshr_count >= m_mshr_limit) return nullptr;

    // Probabilistic injection check.
    double prob = m_adapter->get_injection_prob(m_id);
    if (prob <= 0.0 || m_dist(m_rng) >= prob) return nullptr;

    // Virtual network selection.
    int vnet = m_adapter->select_vnet(m_rng, m_dist);

    // Packet size: vnet0 (requests) and vnet2 (writeback notifications) are
    // always 1-flit control messages.  Only vnet1 (responses) carries data
    // and may be 5 flits.
    int num_flits;
    if (vnet == 1) {
        double data_frac = m_adapter->get_data_frac();
        num_flits = (m_dist(m_rng) < data_frac)
                    ? m_adapter->data_packet_flits()
                    : m_adapter->ctrl_packet_flits();
    } else {
        num_flits = m_adapter->ctrl_packet_flits(); // always 1
    }

    // Destination selection.
    int dest_ni, dest_router;
    if (vnet == 0 || vnet == 2) {
        // Request or writeback -> goes to a directory.
        int dir = m_adapter->select_dest_dir(m_rng, m_dist);
        dest_router = m_adapter->dir_to_router(dir);
        dest_ni     = dest_router; // NI_i connects to router_i in a mesh
    } else {
        // vnet 1 injected by core -> goes to a random other core.
        int num_cpus = m_adapter->num_cpus();
        if (num_cpus <= 1) return nullptr;  // no other core to send to
        std::uniform_int_distribution<int> core_dist(0, num_cpus - 2);
        int r = core_dist(m_rng);
        dest_ni     = (r >= m_id) ? r + 1 : r;
        dest_router = m_net_ptr->get_router_id(dest_ni, vnet);
    }

    generate_packet(dest_ni, dest_router, vnet, num_flits, t);
    ++m_injected_packets;

    if (m_trace) {
        std::cout << "PACE: node " << m_id << " injects vnet=" << vnet
                  << " flits=" << num_flits
                  << " dest_ni=" << dest_ni << " t=" << t << "\n";
    }

    // MSHR: only track vnet 0 (requests).
    if (vnet == 0) {
        ++m_mshr_count;
        if (m_mshr_count > m_max_mshr_count)
            m_max_mshr_count = m_mshr_count;
        m_adapter->record_mshr_sample(m_id, m_mshr_count);
    }

    flit* fl = m_flit_queue.front();
    m_flit_queue.pop();
    fl->set_enqueue_time(t);
    return fl;
}

void PaceTrafficGenerator::receive_flit(flit* flt)
{
    uint64_t t = current_time();

    // Only count completed packets (TAIL or HEAD_TAIL flits).
    if (flt->get_type() == TAIL_ || flt->get_type() == HEAD_TAIL_) {
        uint64_t latency = t - flt->get_creation_time();
        ++m_received_packets;
        m_total_latency += latency;

        int vnet = flt->get_vnet();
        if (vnet < 3) {
            m_received_per_vnet[vnet]++;
            m_latency_per_vnet[vnet] += latency;
        }

        // Count packet size for flit accounting (size is stored in flit).
        int num_flits = flt->get_size();
        m_adapter->record_packet_received(latency,
                                          m_adapter->current_phase_idx(),
                                          vnet, num_flits);

        // MSHR decrement: any vnet 1 arriving at a core frees a slot.
        if (m_is_core && vnet == 1) {
            if (m_mshr_count > 0) {
                --m_mshr_count;
                m_adapter->record_mshr_sample(m_id, m_mshr_count);
            }
        }

        // Response generation: directories respond to requests.
        if (m_is_directory && (vnet == 0 || vnet == 2)) {
            int resp_flits;
            if (vnet == 2) {
                // Writeback acknowledgement is always 1 flit.
                resp_flits = 1;
            } else {
                // vnet 0 request: correlated response sizing.
                resp_flits = m_adapter->select_response_size(m_rng, m_dist);
            }
            ResponseJob job;
            job.dest_ni       = flt->get_route().src_ni;
            job.dest_router   = flt->get_route().src_router;
            job.num_flits     = resp_flits;
            job.creation_time = t;
            m_pending_responses.push(job);
        }
    }

    delete flt;
}

// ============================================================
// PaceAdapter — implementation
// ============================================================

PaceAdapter::PaceAdapter(const std::string& profile_path,
                         int mshr_limit, int seed,
                         const AblationConfig& ablation)
    : m_current_phase(0), m_cycles_in_phase(0), m_done(false),
      m_mshr_limit(mshr_limit), m_seed(seed),
      m_total_latency_sum(0), m_total_packets_received(0),
      m_total_flits_received(0),
      m_num_routers(0), m_ablation(ablation)
{
    m_profile = PaceProfile::load(profile_path);

    // --pace-no-phases: collapse all phases into a single aggregate phase.
    if (m_ablation.no_phases && m_profile.phases.size() > 1) {
        uint64_t total_net_cycles = 0;
        int64_t  total_pkts = 0, total_flits_sum = 0;
        for (const auto& ph : m_profile.phases) {
            total_net_cycles += ph.network_cycles;
            total_pkts       += ph.total_packets;
            total_flits_sum  += ph.total_flits;
        }

        PacePhase agg;
        agg.phase_index      = 0;
        agg.network_cycles   = total_net_cycles;
        agg.total_packets    = total_pkts;
        agg.total_flits      = total_flits_sum;
        agg.flits_per_packet = total_pkts > 0
                               ? (double)total_flits_sum / total_pkts : 1.0;

        // Weighted avg lambda (weight = cycles per phase).
        double lambda_sum = 0.0;
        for (const auto& ph : m_profile.phases)
            lambda_sum += ph.lambda * ph.network_cycles;
        agg.lambda = total_net_cycles > 0 ? lambda_sum / total_net_cycles : 0.0;

        // Weighted avg data_pct (weight = total_packets per phase).
        double data_pct_sum = 0.0;
        for (const auto& ph : m_profile.phases)
            data_pct_sum += ph.data_pct * ph.total_packets;
        agg.data_pct = total_pkts > 0 ? data_pct_sum / total_pkts : 0.0;
        agg.ctrl_pct = 100.0 - agg.data_pct;

        // Sum vnet_packets.
        for (const auto& ph : m_profile.phases)
            for (const auto& kv : ph.vnet_packets)
                agg.vnet_packets[kv.first] += kv.second;

        // Weighted per_router_injection (weight = total_packets per phase).
        for (const auto& ph : m_profile.phases)
            for (const auto& kv : ph.per_router_injection)
                agg.per_router_injection[kv.first] += kv.second * ph.total_packets;
        if (total_pkts > 0)
            for (auto& kv : agg.per_router_injection)
                kv.second /= total_pkts;

        // Weighted dir_fractions (weight = vnet0+vnet2 packets per phase).
        double v02_total = 0.0;
        for (const auto& ph : m_profile.phases) {
            auto v0it = ph.vnet_packets.find(0);
            auto v2it = ph.vnet_packets.find(2);
            double v02 = (v0it != ph.vnet_packets.end() ? (double)v0it->second : 0.0)
                       + (v2it != ph.vnet_packets.end() ? (double)v2it->second : 0.0);
            v02_total += v02;
            for (const auto& kv : ph.dir_fractions)
                agg.dir_fractions[kv.first] += kv.second * v02;
        }
        if (v02_total > 0.0)
            for (auto& kv : agg.dir_fractions)
                kv.second /= v02_total;

        // Weighted avg_packet_latency (weight = total_packets).
        double lat_sum = 0.0;
        for (const auto& ph : m_profile.phases)
            lat_sum += ph.avg_packet_latency * ph.total_packets;
        agg.avg_packet_latency = total_pkts > 0 ? lat_sum / total_pkts : 0.0;
        agg.sim_ticks = 0;

        // Derive per_router_prob.
        for (const auto& kv : agg.per_router_injection)
            agg.per_router_prob[kv.first] =
                agg.lambda * m_profile.num_cpus * kv.second;

        // Derive vnet selection probabilities.
        int64_t agg_total_pkt = total_pkts > 0 ? total_pkts : 1;
        auto vpkt = [&](int v) -> int64_t {
            auto it = agg.vnet_packets.find(v);
            return it != agg.vnet_packets.end() ? it->second : 0;
        };
        agg.vnet0_prob = (double)vpkt(0) / agg_total_pkt;
        agg.vnet1_prob = (double)vpkt(1) / agg_total_pkt;
        agg.vnet2_prob = 1.0 - agg.vnet0_prob - agg.vnet1_prob;
        if (agg.vnet2_prob < 0.0) agg.vnet2_prob = 0.0;

        // Derive cumulative dir_fractions.
        double cum = 0.0;
        for (const auto& kv : agg.dir_fractions) {
            cum += kv.second;
            agg.dir_cumulative.push_back({kv.first, cum});
        }

        // Derive response_data_prob.
        int64_t v0f = vpkt(0);
        int64_t v2f = vpkt(2) * m_profile.model.data_packet_flits;
        int64_t v1f = total_flits_sum - v0f - v2f;
        int64_t v1p = vpkt(1) > 0 ? vpkt(1) : 1;
        double data_resp = (double)(v1f - v1p) / (4.0 * v1p);
        agg.response_data_prob = std::max(0.0, std::min(1.0, data_resp));

        m_profile.phases    = {agg};
        m_profile.num_phases = 1;
        std::cout << "PACE: --pace-no-phases: collapsed to 1 aggregate phase"
                  << " (cycles=" << agg.network_cycles
                  << " lambda=" << agg.lambda << ")\n";
    }

    m_phase_metrics.resize(m_profile.num_phases);

    std::cout << "PACE: loaded profile \"" << profile_path << "\"\n"
              << "  num_cpus=" << m_profile.num_cpus
              << "  num_dirs=" << m_profile.num_dirs
              << "  num_phases=" << m_profile.num_phases << "\n";
    for (int i = 0; i < m_profile.num_phases; ++i) {
        const auto& ph = m_profile.phases[i];
        std::cout << "  phase[" << i << "]: "
                  << "cycles=" << ph.network_cycles
                  << " lambda=" << ph.lambda
                  << " data_pct=" << ph.data_pct << "%\n";
    }
}

PaceAdapter::~PaceAdapter()
{
    for (auto tg : m_tgs) delete tg;
}

void PaceAdapter::init(const std::vector<NetworkInterface*>& nis,
                       GarnetNetwork* net)
{
    int num_nis = (int)nis.size();
    m_num_routers = num_nis; // NI count == router count in this topology

    // Build reverse map: router_id -> dir_id (for quick lookup).
    std::map<int, int> router_to_dir;
    for (const auto& kv : m_profile.directory_remapping)
        router_to_dir[kv.second] = kv.first;

    m_tgs.reserve(num_nis);

    // MSHR tracking arrays — one entry per CPU node.
    int num_cpus = m_profile.num_cpus;
    m_mshr_sum.assign(num_cpus, 0.0);
    m_mshr_sample_count.assign(num_cpus, 0);
    m_max_mshr_per_node.assign(num_cpus, 0);

    for (int i = 0; i < num_nis; ++i) {
        NetworkInterface* ni = nis[i];
        int router_id = ni->get_router_id(0); // vnet 0 router

        // NI index i == router_id in this topology (each router has exactly
        // one NI and they are numbered identically).  NIs 0..num_cpus-1 are
        // core nodes; NIs num_cpus..num_nis-1 are directory/IO nodes.
        // per_router_injection in the profile may include entries for all
        // routers (including directories), but directory TGs never reach the
        // injection probability check — the !m_is_core guard in send_flit()
        // exits before get_injection_prob() is called.
        bool is_core = (i < num_cpus);
        bool is_dir  = (router_to_dir.count(router_id) > 0);

        PaceTrafficGenerator* tg = new PaceTrafficGenerator(
            i, net, ni, this,
            is_core, is_dir,
            m_mshr_limit, m_seed);

        m_tgs.push_back(tg);
        ni->setTrafficGenerator(tg);  // replaces the old SimpleTrafficGenerator
    }

    std::cout << "PACE: created " << num_nis << " PaceTrafficGenerators"
              << "  (" << num_cpus << " cores, "
              << m_profile.num_dirs << " dirs)\n";
    for (const auto& kv : m_profile.directory_remapping)
        std::cout << "  dir " << kv.first << " -> router " << kv.second << "\n";
}

bool PaceAdapter::tick(uint64_t /*current_cycle*/)
{
    if (m_done) return false;

    ++m_cycles_in_phase;

    const PacePhase& ph = m_profile.phases[m_current_phase];
    // A phase with network_cycles == 0 (both network_cycles and sim_ticks were
    // zero in the profile — degenerate) is treated as zero-duration and skipped.
    if (ph.network_cycles == 0 || m_cycles_in_phase >= ph.network_cycles) {
        // Advance to next phase without resetting MSHR state.
        ++m_current_phase;
        m_cycles_in_phase = 0;
        if (m_current_phase >= (int)m_profile.phases.size()) {
            m_done = true;
            return false;
        }
        std::cout << "PACE: entering phase " << m_current_phase << "\n";
    }
    return true;
}

uint64_t PaceAdapter::total_network_cycles() const
{
    // network_cycles is guaranteed non-zero here: PaceProfile::load() falls
    // back to sim_ticks/333 when the gem5 network.cycles stat is absent.
    uint64_t total = 0;
    for (const auto& ph : m_profile.phases)
        total += ph.network_cycles;
    return total;
}

double PaceAdapter::get_injection_prob(int router_id) const
{
    const auto& ph = current_phase();
    // --pace-no-per-source: all routers use the same average rate (lambda).
    if (m_ablation.no_per_source)
        return ph.lambda;
    auto it = ph.per_router_prob.find(router_id);
    return (it != ph.per_router_prob.end()) ? it->second : 0.0;
}

double PaceAdapter::get_data_frac() const
{
    return current_phase().data_pct / 100.0;
}

int PaceAdapter::select_vnet(std::mt19937& rng,
                              std::uniform_real_distribution<double>& dist) const
{
    const PacePhase& ph = current_phase();
    double r = dist(rng);
    if (r < ph.vnet0_prob) return 0;
    if (r < ph.vnet0_prob + ph.vnet1_prob) return 1;
    return 2;
}

int PaceAdapter::select_dest_dir(std::mt19937& rng,
                                  std::uniform_real_distribution<double>& dist) const
{
    // --pace-no-weighted-dest: uniform random directory selection.
    if (m_ablation.no_weighted_dest) {
        std::uniform_int_distribution<int> uid(0, m_profile.num_dirs - 1);
        return uid(rng);
    }
    const PacePhase& ph = current_phase();
    double r = dist(rng);
    for (const auto& entry : ph.dir_cumulative) {
        if (r < entry.second) return entry.first;
    }
    // Fallback: return last dir.
    return ph.dir_cumulative.back().first;
}

int PaceAdapter::dir_to_router(int dir_id) const
{
    // --pace-no-remap: identity mapping (dir d -> router d, modulo num_routers).
    if (m_ablation.no_remap) {
        int n = m_num_routers > 0 ? m_num_routers : m_profile.num_dirs;
        return dir_id % n;
    }
    auto it = m_profile.directory_remapping.find(dir_id);
    if (it != m_profile.directory_remapping.end()) return it->second;
    return dir_id; // identity fallback
}

int PaceAdapter::select_response_size(std::mt19937& rng,
                                       std::uniform_real_distribution<double>& dist) const
{
    // --pace-no-corr-response: always return fixed data packet size (5 flits).
    if (m_ablation.no_corr_response)
        return m_profile.model.data_packet_flits;
    double p = current_phase().response_data_prob;
    return (dist(rng) < p) ? m_profile.model.data_packet_flits
                           : m_profile.model.ctrl_packet_flits;
}

void PaceAdapter::record_packet_received(uint64_t latency, int phase_idx,
                                          int /*vnet*/, int num_flits)
{
    m_latency_histogram[latency]++;
    m_total_latency_sum += latency;
    ++m_total_packets_received;
    m_total_flits_received += (uint64_t)num_flits;

    if (phase_idx >= 0 && phase_idx < (int)m_phase_metrics.size()) {
        m_phase_metrics[phase_idx].packets_received++;
        m_phase_metrics[phase_idx].total_latency += latency;
        m_phase_metrics[phase_idx].flits_received += (uint64_t)num_flits;
    }
}

void PaceAdapter::record_mshr_sample(int node_id, int count)
{
    if (node_id < 0 || node_id >= (int)m_mshr_sum.size()) return;
    m_mshr_sum[node_id] += count;
    m_mshr_sample_count[node_id]++;
    if (count > m_max_mshr_per_node[node_id])
        m_max_mshr_per_node[node_id] = count;
}

// ---- 99th-percentile helper ----
static uint64_t percentile99(const std::map<uint64_t, uint64_t>& hist)
{
    if (hist.empty()) return 0;
    uint64_t total = 0;
    for (const auto& kv : hist) total += kv.second;
    uint64_t threshold = (uint64_t)std::ceil(0.99 * total);
    uint64_t cumulative = 0;
    for (const auto& kv : hist) {
        cumulative += kv.second;
        if (cumulative >= threshold) return kv.first;
    }
    return hist.rbegin()->first;
}

void PaceAdapter::dump_results(const std::string& path,
                                const std::vector<NetworkLink*>& links,
                                uint64_t total_cycles) const
{
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "PACE: cannot write results to " << path << "\n";
        return;
    }

    double avg_lat = m_total_packets_received > 0
                     ? (double)m_total_latency_sum / m_total_packets_received
                     : 0.0;
    uint64_t p99   = percentile99(m_latency_histogram);
    double throughput = total_cycles > 0
                        ? (double)m_total_flits_received / total_cycles
                        : 0.0;

    // MSHR saturation analysis.
    double max_avg_mshr = 0.0;
    std::vector<int> saturated_nodes;
    for (int i = 0; i < (int)m_mshr_sum.size(); ++i) {
        double avg_mshr = m_mshr_sample_count[i] > 0
                          ? m_mshr_sum[i] / m_mshr_sample_count[i]
                          : 0.0;
        if (avg_mshr > max_avg_mshr) max_avg_mshr = avg_mshr;
        if (avg_mshr > 14.0 * m_mshr_limit / 16.0)
            saturated_nodes.push_back(i);
    }
    bool near_sat = !saturated_nodes.empty();

    // Link utilizations.
    double link_util_sum = 0.0, link_util_max = 0.0;
    int num_links = (int)links.size();
    for (auto* lk : links) {
        double u = total_cycles > 0
                   ? (double)lk->getLinkUtilization() / total_cycles
                   : 0.0;
        link_util_sum += u;
        if (u > link_util_max) link_util_max = u;
    }
    double link_util_avg = num_links > 0 ? link_util_sum / num_links : 0.0;

    // ---- Write JSON ----
    f << "{\n";

    // ablation config (true = feature enabled; false = feature disabled/ablated)
    f << "  \"ablation\": {\n"
      << "    \"per_source\": "    << (!m_ablation.no_per_source    ? "true" : "false") << ",\n"
      << "    \"phases\": "        << (!m_ablation.no_phases        ? "true" : "false") << ",\n"
      << "    \"mshr\": "          << (!m_ablation.no_mshr          ? "true" : "false") << ",\n"
      << "    \"remap\": "         << (!m_ablation.no_remap         ? "true" : "false") << ",\n"
      << "    \"weighted_dest\": " << (!m_ablation.no_weighted_dest ? "true" : "false") << ",\n"
      << "    \"corr_response\": " << (!m_ablation.no_corr_response ? "true" : "false") << "\n"
      << "  },\n";

    // simulation_summary
    f << "  \"simulation_summary\": {\n"
      << "    \"total_cycles\": " << total_cycles << ",\n"
      << "    \"total_phases\": " << m_profile.num_phases << ",\n"
      << "    \"mshr_limit\": " << m_mshr_limit << ",\n"
      << "    \"num_cpus\": " << m_profile.num_cpus << ",\n"
      << "    \"num_dirs\": " << m_profile.num_dirs << "\n"
      << "  },\n";

    // packet_stats
    f << "  \"packet_stats\": {\n"
      << "    \"total_packets_received\": " << m_total_packets_received << ",\n"
      << "    \"total_flits_received\": " << m_total_flits_received << ",\n"
      << "    \"avg_latency_cycles\": " << avg_lat << ",\n"
      << "    \"p99_latency_cycles\": " << p99 << ",\n"
      << "    \"throughput_flits_per_cycle\": " << throughput << "\n"
      << "  },\n";

    // saturation
    f << "  \"saturation\": {\n"
      << "    \"near_saturation\": " << (near_sat ? "true" : "false") << ",\n"
      << "    \"max_avg_mshr_occupancy\": " << max_avg_mshr << ",\n"
      << "    \"saturated_nodes\": [";
    for (int i = 0; i < (int)saturated_nodes.size(); ++i) {
        if (i > 0) f << ", ";
        f << saturated_nodes[i];
    }
    f << "]\n  },\n";

    // per_phase_stats
    f << "  \"per_phase_stats\": [\n";
    for (int i = 0; i < (int)m_phase_metrics.size(); ++i) {
        const auto& pm = m_phase_metrics[i];
        double ph_avg = pm.packets_received > 0
                        ? (double)pm.total_latency / pm.packets_received
                        : 0.0;
        f << "    {\n"
          << "      \"phase_index\": " << i << ",\n"
          << "      \"packets_received\": " << pm.packets_received << ",\n"
          << "      \"flits_received\": " << pm.flits_received << ",\n"
          << "      \"avg_latency_cycles\": " << ph_avg << ",\n"
          << "      \"profile_lambda\": " << m_profile.phases[i].lambda << ",\n"
          << "      \"profile_avg_latency\": " << m_profile.phases[i].avg_packet_latency
          << "\n    }";
        if (i + 1 < (int)m_phase_metrics.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // per_link_utilization
    f << "  \"per_link_utilization\": {\n"
      << "    \"num_links\": " << num_links << ",\n"
      << "    \"avg\": " << link_util_avg << ",\n"
      << "    \"max\": " << link_util_max << "\n"
      << "  },\n";

    // latency_histogram (top 20 buckets by count for compactness)
    f << "  \"latency_histogram_sample\": {";
    int bucket_count = 0;
    for (const auto& kv : m_latency_histogram) {
        if (bucket_count > 0) f << ",";
        f << "\n    \"" << kv.first << "\": " << kv.second;
        if (++bucket_count >= 50) break;
    }
    f << "\n  }\n";

    f << "}\n";
    f.close();

    std::cout << "PACE: results written to " << path << "\n";
    std::cout << "  avg_latency=" << avg_lat << " cycles"
              << "  p99=" << p99 << " cycles"
              << "  throughput=" << throughput << " flits/cycle\n";
    if (near_sat) {
        std::cout << "  WARNING: " << saturated_nodes.size()
                  << " node(s) near MSHR saturation\n";
    }
}

void PaceAdapter::set_directory_remapping(const std::map<int,int>& remap)
{
    m_profile.directory_remapping = remap;
    std::cout << "PACE: directory remapping overridden via --pace-dir-routers:\n";
    for (const auto& kv : remap)
        std::cout << "  dir " << kv.first << " -> router " << kv.second << "\n";
}

} // namespace garnet

// PACE Adapter + PACE Traffic Generator — full implementations.

#include "PaceAdapter.hh"

#include <cassert>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <numeric>
#include <set>
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
      m_mshr_limit(mshr_limit), m_pending_requests(0), m_max_mshr_count(0),
      m_mshr_stall_cycles(0),
      m_is_bursting(false), m_prob_stay_on(0.0), m_prob_stay_off(1.0),
      m_last_phase_idx(-1),
      m_stalled_flit(nullptr),
      m_last_injection_cycle(static_cast<uint64_t>(-1)),
      m_last_drain_cycle(static_cast<uint64_t>(-1)),
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

void PaceTrafficGenerator::update_burst_parameters(double lambda, double variance)
{
    // Step 3: calculate state transition probabilities
    double Cv2 = (lambda > 0.0) ? variance / (1.0 / (lambda * lambda)) : 0.0;
    // (User said Cv2 = variance * (lambda * lambda). 
    // Standard def for inter-arrival time is mean=1/lambda, var=variance. 
    // Cv2 = variance / (mean^2) = variance * lambda^2. Correct.)
    Cv2 = variance * (lambda * lambda);

    if (lambda >= 1.0) {
        m_prob_stay_on  = 1.0;
        m_prob_stay_off = 0.0;
    } else if (Cv2 > 1.0) {
        // peak_rate = 1.0
        m_prob_stay_on  = 1.0 - (1.0 / (Cv2 + 1.0));
        m_prob_stay_off = 1.0 - (lambda / (1.0 - lambda)) * (1.0 - m_prob_stay_on);
    } else {
        m_prob_stay_on  = 0.0;
        m_prob_stay_off = 1.0 - lambda;
    }
}

flit* PaceTrafficGenerator::send_flit()
{
    uint64_t t = current_time();

    // Step 2 Hack: drain MSHRs (pending requests)
    if (m_pending_requests > 0 && t != m_last_drain_cycle) {
        m_last_drain_cycle = t;
        // Drain probability: 1.0 / avg_lat. 
        // We use current phase's avg_packet_latency.
        double avg_lat = m_adapter->get_avg_latency();
        if (avg_lat > 0.0) {
            if (m_dist(m_rng) < (1.0 / avg_lat)) {
                --m_pending_requests;
                m_adapter->record_mshr_sample(m_id, m_pending_requests);
            }
        }
    }

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
    if (!m_is_core) return nullptr;

    // No new injection after all phases are exhausted (drain window).
    if (m_adapter->is_done()) return nullptr;

    // Update phase-specific parameters if needed
    int current_ph = m_adapter->current_phase_idx();
    if (current_ph != m_last_phase_idx) {
        double ph_lambda = m_adapter->get_injection_prob(m_id);
        double ph_variance = m_adapter->get_variance();
        update_burst_parameters(ph_lambda, ph_variance);
        m_mshr_limit = m_adapter->get_mshr_limit();
        m_last_phase_idx = current_ph;
    }

    // Step 3: Probabilistic injection check with Burst Model
    double lambda = m_adapter->get_injection_prob(m_id);
    double variance = m_adapter->get_variance();
    double Cv2 = variance * (lambda * lambda);

    bool would_inject = false;
    // --burst-model=off forces smooth Poisson regardless of variance
    bool use_burst = (Cv2 > 1.0) && !m_adapter->no_burst();
    if (!use_burst) {
        // Smooth traffic: standard uniform random (Poisson)
        if (lambda > 0.0 && m_dist(m_rng) < lambda) would_inject = true;
    } else {
        // Bursty traffic: ON/OFF Markov state machine
        if (m_is_bursting) {
            would_inject = true; // ON state
            // Transition check for NEXT cycle
            if (m_dist(m_rng) > m_prob_stay_on) m_is_bursting = false;
        } else {
            // Transition check for NEXT cycle
            if (m_dist(m_rng) > m_prob_stay_off) m_is_bursting = true;
            would_inject = false; // OFF state
        }
    }

    if (!would_inject) return nullptr;

    // Step 2: Hard MSHR cap (skipped when --pace-no-mshr is active).
    if (!m_adapter->no_mshr() && m_pending_requests >= m_mshr_limit) {
        m_mshr_stall_cycles++;
        return nullptr;
    }

    bool should_inject = true; // we already checked would_inject and MSHR

    if (!should_inject) return nullptr;

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
        dest_ni     = m_adapter->dir_to_ni(dir); // first NI on that router
    } else {
        // vnet 1 injected by core -> goes to a random other core.
        // Use num_nodes() (actual NI count) not num_cpus() (profile value) so
        // dest_ni is always within [0, num_nis-1] regardless of profile mismatch.
        int num_nodes = m_adapter->num_nodes();
        if (num_nodes <= 1) return nullptr;  // no other core to send to
        std::uniform_int_distribution<int> core_dist(0, num_nodes - 2);
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

    // MSHR: track all successful core injections (Step 2)
    ++m_pending_requests;
    if (m_pending_requests > m_max_mshr_count)
        m_max_mshr_count = m_pending_requests;
    m_adapter->record_mshr_sample(m_id, m_pending_requests);

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
        // REMOVED for IPP model hack: we use probabilistic drain in send_flit.

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
                         const AblationConfig& ablation,
                         int packets_per_node, double temporal_floor)
    : m_current_phase(0), m_cycles_in_phase(0), m_packets_in_current_phase(0),
      m_done(false), m_mshr_limit(mshr_limit), m_seed(seed),
      m_total_latency_sum(0), m_total_packets_received(0),
      m_total_flits_received(0),
      m_num_routers(0), m_concentration(1), m_ablation(ablation),
      m_target_packets_per_node(packets_per_node),
      m_temporal_floor(temporal_floor), m_diameter(10)
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
                       GarnetNetwork* net, int diameter)
{
    m_diameter = diameter;
    int num_nis = (int)nis.size();
    m_num_routers = num_nis; // node count (= num_cpus); used for packet threshold

    // Compute concentration: NIs per physical router.
    // For standard topologies (1 NI per router), concentration = 1.
    // For CMesh (multiple NIs per router), concentration = num_nis / num_physical_routers.
    {
        std::set<int> unique_routers;
        for (auto ni : nis) unique_routers.insert(ni->get_router_id(0));
        int num_physical_routers = (int)unique_routers.size();
        m_concentration = (num_physical_routers > 0 && num_nis > num_physical_routers)
                          ? num_nis / num_physical_routers : 1;
    }

    // Build reverse map: router_id -> dir_id (for quick lookup).
    std::map<int, int> router_to_dir;
    for (const auto& kv : m_profile.directory_remapping)
        router_to_dir[kv.second] = kv.first;

    m_tgs.reserve(num_nis);

    int num_cpus = m_profile.num_cpus;

    // Warn if the topology NI count doesn't match the profile's num_cpus.
    // This usually means --num-cpus was omitted or set incorrectly for CMesh.
    if (num_nis != num_cpus) {
        std::cerr << "PACE WARNING: topology has " << num_nis
                  << " NIs but profile expects num_cpus=" << num_cpus << ".\n";
        if (num_nis < num_cpus)
            std::cerr << "  For CMesh topologies, pass --num-cpus=" << num_cpus
                      << " to create the correct number of NIs.\n";
    }

    // MSHR tracking arrays — sized to cover all actual NI ids (0..num_nis-1).
    // Use max(num_cpus, num_nis) so the array is valid regardless of which is larger.
    int mshr_array_size = std::max(num_cpus, num_nis);
    m_mshr_sum.assign(mshr_array_size, 0.0);
    m_mshr_sample_count.assign(mshr_array_size, 0);
    m_max_mshr_per_node.assign(mshr_array_size, 0);

    for (int i = 0; i < num_nis; ++i) {
        NetworkInterface* ni = nis[i];
        int router_id = ni->get_router_id(0); // vnet 0 router

        // All NIs 0..num_nis-1 are CPU nodes.
        // For CMesh: NI i is on router i/concentration; directory NIs are the
        // *first* NI on each gateway router (i % concentration == 0 && router is dir).
        // For standard: NI i == router i; directories are NIs whose router is a gateway.
        bool is_core = (i < num_cpus);
        bool is_dir  = (router_to_dir.count(router_id) > 0) &&
                       (m_concentration == 1 || (i % m_concentration == 0));

        PaceTrafficGenerator* tg = new PaceTrafficGenerator(
            i, net, ni, this,
            is_core, is_dir,
            m_mshr_limit, m_seed);

        m_tgs.push_back(tg);
        ni->setTrafficGenerator(tg);  // replaces the old SimpleTrafficGenerator
    }

    std::cout << "PACE: created " << num_nis << " PaceTrafficGenerators"
              << "  (" << num_cpus << " cores, "
              << m_profile.num_dirs << " dirs)\n"
              << "  concentration=" << m_concentration
              << " (" << num_nis / m_concentration << " physical routers)\n";
    for (const auto& kv : m_profile.directory_remapping)
        std::cout << "  dir " << kv.first << " -> router " << kv.second << "\n";

    // Startup convergence estimate.
    // Per phase: max(floor_threshold, ceil(packet_threshold / total_injection_rate)).
    // Zero-duration phases cost 1 cycle (skipped immediately).
    uint64_t floor_thresh   = (uint64_t)(m_temporal_floor * m_diameter);
    uint64_t packet_thresh  = (uint64_t)(m_target_packets_per_node * num_nis);
    uint64_t total_net_cyc  = 0;
    uint64_t est_conv_cyc   = 0;
    for (const auto& ph : m_profile.phases) {
        total_net_cyc += ph.network_cycles;
        if (ph.network_cycles == 0) {
            est_conv_cyc += 1; // skipped immediately
        } else {
            double total_rate = ph.lambda * num_cpus; // packets/cycle across all nodes
            uint64_t pkts_est = (total_rate > 0.0)
                ? (uint64_t)std::ceil((double)packet_thresh / total_rate)
                : ph.network_cycles;
            uint64_t phase_est = std::max(floor_thresh, pkts_est);
            // Cap at the phase's own network_cycles (fallback termination).
            est_conv_cyc += std::min(phase_est, ph.network_cycles);
        }
    }
    est_conv_cyc += 200; // drain window

    std::cout << "PACE: " << m_profile.num_phases << " phases"
              << "  total_net_cycles=" << total_net_cyc
              << "  estimated_convergence_cycles=" << est_conv_cyc
              << "  (target=" << m_target_packets_per_node << " packets/node"
              << "  floor=" << floor_thresh << " cycles"
              << "  diameter=" << m_diameter << ")\n";
}

bool PaceAdapter::tick(uint64_t /*current_cycle*/)
{
    if (m_done) return false;

    ++m_cycles_in_phase;

    const PacePhase& ph = m_profile.phases[m_current_phase];

    uint64_t floor_threshold  = (uint64_t)(m_temporal_floor * m_diameter);
    uint64_t packet_threshold = (uint64_t)(100 * m_target_packets_per_node * m_num_routers);
    bool time_floor_met   = m_cycles_in_phase > floor_threshold;
    bool packet_target_met = m_packets_in_current_phase >= packet_threshold;
    // Fallback: phase ran its full allocated duration with no early convergence.
    bool cycles_exhausted = (ph.network_cycles > 0 &&
                             m_cycles_in_phase >= ph.network_cycles);
    // Zero-duration phases (e.g. network.cycles stat missing, lambda≈0): skip.
    bool zero_duration    = (ph.network_cycles == 0);

    bool advance = zero_duration || cycles_exhausted ||
                   (time_floor_met && packet_target_met);

    if (advance) {
        if (zero_duration) {
            std::cout << "PACE: phase " << m_current_phase
                      << " skipped (zero-duration)\n";
        } else if (time_floor_met && packet_target_met) {
            std::cout << "PACE: phase " << m_current_phase << " converged early ("
                      << m_cycles_in_phase << " / " << ph.network_cycles
                      << " cycles, " << m_packets_in_current_phase << " packets)\n";
        } else {
            std::cout << "PACE: phase " << m_current_phase << " exhausted ("
                      << m_cycles_in_phase << " / " << ph.network_cycles
                      << " cycles, " << m_packets_in_current_phase
                      << " / " << packet_threshold << " packets)\n";
        }

        ++m_current_phase;
        m_cycles_in_phase = 0;
        m_packets_in_current_phase = 0;
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

double PaceAdapter::get_injection_prob(int ni_id) const
{
    const auto& ph = current_phase();
    // --pace-no-per-source: all nodes use the same average rate (lambda).
    if (m_ablation.no_per_source)
        return ph.lambda;
    // per_router_prob is keyed by physical router_id.
    // For CMesh (concentration > 1): ni_id / concentration = router_id,
    // and the per-NI rate = per-router rate / concentration.
    int router_id = ni_id / m_concentration;
    auto it = ph.per_router_prob.find(router_id);
    if (it == ph.per_router_prob.end()) return 0.0;
    return it->second / m_concentration;
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
    // --pace-no-remap: identity mapping (dir d -> router d, modulo num_physical_routers).
    if (m_ablation.no_remap) {
        int num_physical = (m_concentration > 0) ? m_num_routers / m_concentration
                                                 : m_num_routers;
        if (num_physical <= 0) num_physical = m_profile.num_dirs;
        return dir_id % num_physical;
    }
    auto it = m_profile.directory_remapping.find(dir_id);
    if (it != m_profile.directory_remapping.end()) return it->second;
    return dir_id; // identity fallback
}

int PaceAdapter::dir_to_ni(int dir_id) const
{
    // Returns the NI id of the directory at dir_id's gateway router.
    // For standard topologies (concentration=1): same as dir_to_router().
    // For CMesh (concentration>1): first NI on the gateway router.
    return dir_to_router(dir_id) * m_concentration;
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
    m_lat_hist.insert(latency);
    m_total_latency_sum += latency;
    ++m_total_packets_received;
    ++m_packets_in_current_phase;
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

std::string PaceAdapter::compute_method() const
{
    // Priority order: first matching flag determines the variant name.
    if (m_ablation.no_per_source)    return "No-PerSource";
    if (m_ablation.no_phases)        return "No-Phases";
    if (m_ablation.no_mshr || m_mshr_limit == 0) return "No-MSHR";
    if (m_ablation.no_remap)         return "No-Remap";
    if (m_ablation.no_weighted_dest) return "No-WeightDest";
    if (m_ablation.no_corr_response) return "No-CorrResp";
    if (m_ablation.no_burst)         return "No-Burst";
    return "pace";
}

void PaceAdapter::dump_results(const std::string& path,
                                const std::vector<NetworkLink*>& links,
                                uint64_t total_cycles) const
{
    dump_results_with_multiplier(path, links, total_cycles, 1.0);
}

void PaceAdapter::dump_results_with_multiplier(
        const std::string& path,
        const std::vector<NetworkLink*>& links,
        uint64_t total_cycles,
        double lambda_multiplier) const
{
    std::ofstream f(path);
    if (!f.is_open()) {
        std::cerr << "PACE: cannot write results to " << path << "\n";
        return;
    }

    double avg_lat = m_total_packets_received > 0
                     ? (double)m_total_latency_sum / m_total_packets_received
                     : 0.0;

    double p50     = m_lat_hist.percentile(0.50);
    double p99     = m_lat_hist.percentile(0.99);
    double p999    = m_lat_hist.percentile(0.999);
    double max_lat = m_lat_hist.max_latency();

    // Aggregate MSHR stalls
    uint64_t total_mshr_stalls = 0;
    uint64_t total_injected = 0;
    uint64_t total_injection_attempts = 0;
    for (auto tg : m_tgs) {
        total_mshr_stalls        += tg->get_mshr_stall_cycles();
        total_injected           += tg->get_injected_packets();
        total_injection_attempts += tg->get_injection_attempts();
    }
    double avg_mshr_stall = m_total_packets_received > 0
                            ? (double)total_mshr_stalls / m_total_packets_received
                            : 0.0;
    double injection_blocked_pct = (total_injection_attempts > 0)
        ? 100.0 * (double)total_mshr_stalls / total_injection_attempts : 0.0;

    // Profile-weighted average latency and per-phase latency list
    double pw_lat_num = 0.0;
    int64_t pw_lat_den = 0;
    std::vector<double> per_phase_lats;
    for (int i = 0; i < (int)m_phase_metrics.size(); ++i) {
        const auto& pm = m_phase_metrics[i];
        double ph_avg = pm.packets_received > 0
                        ? (double)pm.total_latency / pm.packets_received : 0.0;
        per_phase_lats.push_back(ph_avg);
        if (pm.packets_received > 0) {
            int64_t p_orig = m_profile.phases[i].total_packets;
            pw_lat_num += ph_avg * p_orig;
            pw_lat_den += p_orig;
        }
    }
    double profile_weighted_avg_lat = (pw_lat_den > 0)
                                      ? pw_lat_num / pw_lat_den : avg_lat;

    double throughput   = total_cycles > 0
                          ? (double)m_total_flits_received / total_cycles : 0.0;
    double offered_load = total_cycles > 0
                          ? (double)total_injected / total_cycles : 0.0;

    // MSHR saturation analysis
    double max_avg_mshr = 0.0, sum_avg_mshr = 0.0;
    int n_nodes_tracked = 0;
    std::vector<int> saturated_nodes;
    for (int i = 0; i < (int)m_mshr_sum.size(); ++i) {
        if (m_mshr_sample_count[i] == 0) continue;
        double avg_mshr = m_mshr_sum[i] / m_mshr_sample_count[i];
        if (avg_mshr > max_avg_mshr) max_avg_mshr = avg_mshr;
        sum_avg_mshr += avg_mshr;
        n_nodes_tracked++;
        if (m_mshr_limit > 0 && avg_mshr > 14.0 * m_mshr_limit / 16.0)
            saturated_nodes.push_back(i);
    }
    double overall_avg_mshr = n_nodes_tracked > 0
                              ? sum_avg_mshr / n_nodes_tracked : 0.0;
    bool near_sat = !saturated_nodes.empty();
    // Also saturate if MSHR was artificially fully blocked (mshr_limit==0 disables MSHR)
    bool mshr_saturated = near_sat;

    // Link utilization
    struct LinkInfo { int idx; double util; };
    std::vector<LinkInfo> link_utils;
    double link_util_sum = 0.0, link_util_max = 0.0;
    int num_links = (int)links.size();
    for (int li = 0; li < num_links; ++li) {
        double u = total_cycles > 0
                   ? (double)links[li]->getLinkUtilization() / total_cycles
                   : 0.0;
        link_utils.push_back({li, u});
        link_util_sum += u;
        if (u > link_util_max) link_util_max = u;
    }
    double link_util_avg = num_links > 0 ? link_util_sum / num_links : 0.0;
    std::sort(link_utils.begin(), link_utils.end(),
              [](const LinkInfo& a, const LinkInfo& b){
                  return a.util > b.util;
              });

    // Method string and topo_config
    std::string method     = compute_method();
    std::string topo_cfg   = m_topo_id.empty() ? ""
        : m_topo_id + "_lat" + std::to_string(m_inter_latency)
          + "_w" + std::to_string(m_inter_width);
    std::string benchmark  = m_profile.benchmark.empty() ? "unknown"
                                                         : m_profile.benchmark;
    double effective_lambda = m_profile.effective_lambda * lambda_multiplier;

    // ---- Write JSON ----
    f << std::fixed << std::setprecision(6);
    f << "{\n";

    // ── Spec B.2 top-level fields (flat, required by analysis scripts) ──
    f << "  \"method\": \""             << method    << "\",\n"
      << "  \"benchmark\": \""          << benchmark << "\",\n"
      << "  \"topo_id\": \""            << m_topo_id << "\",\n"
      << "  \"inter_latency\": "        << m_inter_latency << ",\n"
      << "  \"inter_width\": "          << m_inter_width   << ",\n"
      << "  \"topo_config\": \""        << topo_cfg        << "\",\n"
      << "  \"avg_packet_latency\": "   << avg_lat         << ",\n"
      << "  \"avg_flit_latency\": "     << avg_lat         << ",\n"  // approx
      << "  \"p50_latency\": "          << p50             << ",\n"
      << "  \"p99_latency\": "          << p99             << ",\n"
      << "  \"p999_latency\": "         << p999            << ",\n"
      << "  \"max_latency\": "          << max_lat         << ",\n"
      << "  \"throughput_flits_per_cycle\": "      << throughput              << ",\n"
      << "  \"offered_load_flits_per_cycle\": "    << offered_load            << ",\n"
      << "  \"accepted_load_flits_per_cycle\": "   << throughput              << ",\n"
      << "  \"injection_blocked_pct\": "           << injection_blocked_pct   << ",\n"
      << "  \"mshr_saturated\": "       << (mshr_saturated ? "true" : "false") << ",\n"
      << "  \"raw_total_packets\": "    << m_total_packets_received << ",\n"
      << "  \"raw_total_flits\": "      << m_total_flits_received   << ",\n"
      << "  \"simulated_cycles\": "     << total_cycles             << ",\n"
      << "  \"lambda_multiplier\": "    << lambda_multiplier        << ",\n"
      << "  \"effective_lambda\": "     << effective_lambda         << ",\n"
      << "  \"profile_weighted_avg_latency\": " << profile_weighted_avg_lat << ",\n"
      << "  \"per_phase_latencies\": [";
    for (int i = 0; i < (int)per_phase_lats.size(); ++i) {
        if (i > 0) f << ", ";
        f << per_phase_lats[i];
    }
    f << "],\n";

    // ── Latency histogram (top-level, spec B.2) ──
    f << "  \"latency_histogram\": {\n"
      << "    \"fine_counts\": [";
    for (size_t i = 0; i < m_lat_hist.fine.size(); ++i) {
        if (i > 0) f << ", "; f << m_lat_hist.fine[i];
    }
    f << "],\n    \"coarse_counts\": [";
    for (size_t i = 0; i < m_lat_hist.coarse.size(); ++i) {
        if (i > 0) f << ", "; f << m_lat_hist.coarse[i];
    }
    f << "],\n    \"ultra_counts\": [";
    for (size_t i = 0; i < m_lat_hist.ultra.size(); ++i) {
        if (i > 0) f << ", "; f << m_lat_hist.ultra[i];
    }
    f << "],\n"
      << "    \"overflow_count\": " << m_lat_hist.overflow << "\n"
      << "  },\n";

    // ── saturation_detail (spec B.2 name) ──
    f << "  \"saturation_detail\": {\n"
      << "    \"max_avg_mshr_occupancy\": "       << max_avg_mshr     << ",\n"
      << "    \"avg_mshr_occupancy_all_nodes\": " << overall_avg_mshr << ",\n"
      << "    \"saturated_nodes\": [";
    for (int i = 0; i < (int)saturated_nodes.size(); ++i) {
        if (i > 0) f << ", ";
        f << saturated_nodes[i];
    }
    f << "],\n"
      << "    \"pct_cycles_mshr_full\": " << injection_blocked_pct << "\n"
      << "  },\n";

    // ── link_utilization (spec B.2) ──
    f << "  \"link_utilization\": {\n"
      << "    \"max_link_util\": " << link_util_max << ",\n"
      << "    \"avg_link_util\": " << link_util_avg << ",\n"
      << "    \"top5_links\": [";
    int top5_count = std::min((int)link_utils.size(), 5);
    for (int i = 0; i < top5_count; ++i) {
        if (i > 0) f << ", ";
        f << "{\"link_idx\": " << link_utils[i].idx
          << ", \"utilization\": " << link_utils[i].util << "}";
    }
    f << "]\n  },\n";

    // ── buffer_pressure (placeholder — VC occupancy not yet tracked) ──
    f << "  \"buffer_pressure\": {\n"
      << "    \"max_avg_vc_occupancy\": 0.0,\n"
      << "    \"pct_vc_cycles_above_80\": 0.0\n"
      << "  },\n";

    // ── Backward-compat nested fields ──
    f << "  \"ablation\": {\n"
      << "    \"per_source\": "    << (!m_ablation.no_per_source    ? "true" : "false") << ",\n"
      << "    \"phases\": "        << (!m_ablation.no_phases        ? "true" : "false") << ",\n"
      << "    \"mshr\": "          << (!m_ablation.no_mshr          ? "true" : "false") << ",\n"
      << "    \"remap\": "         << (!m_ablation.no_remap         ? "true" : "false") << ",\n"
      << "    \"weighted_dest\": " << (!m_ablation.no_weighted_dest ? "true" : "false") << ",\n"
      << "    \"corr_response\": " << (!m_ablation.no_corr_response ? "true" : "false") << ",\n"
      << "    \"burst\": "         << (!m_ablation.no_burst         ? "true" : "false") << "\n"
      << "  },\n";

    f << "  \"simulation_summary\": {\n"
      << "    \"total_cycles\": "      << total_cycles         << ",\n"
      << "    \"total_phases\": "      << m_profile.num_phases << ",\n"
      << "    \"mshr_limit\": "        << m_mshr_limit         << ",\n"
      << "    \"num_cpus\": "          << m_profile.num_cpus   << ",\n"
      << "    \"num_dirs\": "          << m_profile.num_dirs   << ",\n"
      << "    \"lambda_multiplier\": " << lambda_multiplier    << "\n"
      << "  },\n";

    f << "  \"packet_stats\": {\n"
      << "    \"total_packets_received\": "      << m_total_packets_received   << ",\n"
      << "    \"total_flits_received\": "        << m_total_flits_received     << ",\n"
      << "    \"avg_latency_cycles\": "           << avg_lat                   << ",\n"
      << "    \"p50_latency_cycles\": "           << p50                       << ",\n"
      << "    \"p99_latency_cycles\": "           << p99                       << ",\n"
      << "    \"p999_latency_cycles\": "          << p999                      << ",\n"
      << "    \"max_latency_cycles\": "           << max_lat                   << ",\n"
      << "    \"avg_mshr_stall_cycles\": "        << avg_mshr_stall            << ",\n"
      << "    \"avg_total_latency_cycles\": "     << avg_lat + avg_mshr_stall  << ",\n"
      << "    \"profile_weighted_avg_latency\": " << profile_weighted_avg_lat  << ",\n"
      << "    \"throughput_flits_per_cycle\": "   << throughput                << ",\n"
      << "    \"offered_load_flits_per_cycle\": " << offered_load              << ",\n"
      << "    \"accepted_load_flits_per_cycle\": " << throughput               << ",\n"
      << "    \"injection_blocked_pct\": "        << injection_blocked_pct     << "\n"
      << "  },\n";

    f << "  \"saturation\": {\n"
      << "    \"near_saturation\": "             << (near_sat ? "true" : "false") << ",\n"
      << "    \"max_avg_mshr_occupancy\": "       << max_avg_mshr     << ",\n"
      << "    \"avg_mshr_occupancy_all_nodes\": " << overall_avg_mshr << ",\n"
      << "    \"saturated_nodes\": [";
    for (int i = 0; i < (int)saturated_nodes.size(); ++i) {
        if (i > 0) f << ", ";
        f << saturated_nodes[i];
    }
    f << "]\n  },\n";

    f << "  \"per_phase_stats\": [\n";
    for (int i = 0; i < (int)m_phase_metrics.size(); ++i) {
        const auto& pm = m_phase_metrics[i];
        double ph_avg = pm.packets_received > 0
                        ? (double)pm.total_latency / pm.packets_received : 0.0;
        f << "    {\n"
          << "      \"phase_index\": "        << i               << ",\n"
          << "      \"packets_received\": "   << pm.packets_received << ",\n"
          << "      \"flits_received\": "     << pm.flits_received   << ",\n"
          << "      \"avg_latency_cycles\": " << ph_avg              << ",\n"
          << "      \"profile_lambda\": "     << m_profile.phases[i].lambda << ",\n"
          << "      \"profile_avg_latency\": "
          << m_profile.phases[i].avg_packet_latency << "\n    }";
        if (i + 1 < (int)m_phase_metrics.size()) f << ",";
        f << "\n";
    }
    f << "  ]\n";

    f << "}\n";
    f.close();

    std::cout << "PACE: results written to " << path << "\n"
              << "  method=" << method
              << "  avg_lat=" << avg_lat
              << "  p50=" << p50
              << "  p99=" << p99
              << "  p999=" << p999
              << "  throughput=" << throughput << " flits/cycle\n"
              << "  injection_blocked=" << injection_blocked_pct << "%\n";
    if (near_sat)
        std::cout << "  WARNING: " << saturated_nodes.size()
                  << " node(s) near MSHR saturation\n";
}

void PaceAdapter::scale_lambda(double multiplier)
{
    if (multiplier <= 0.0) multiplier = 0.001;
    for (auto& ph : m_profile.phases) {
        ph.lambda *= multiplier;
        for (auto& kv : ph.per_router_prob)
            kv.second *= multiplier;
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

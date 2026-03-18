#include "Topology.hh"
#include "FileTopology.hh"
#include <iostream>
#include <unistd.h>
#include <queue>
#include <climits>
#include <set>
#include <functional>

namespace garnet {

Topology::Topology(GarnetNetwork* net, int num_rows, int num_cols, int num_depth)
    : m_net(net), m_rows(num_rows), m_cols(num_cols), m_depth(num_depth),
      m_link_id_counter(0)
{
    m_num_vns      = 2; // Default; override with set_num_vnets() before build()
    m_vcs_per_vnet = 4; // Default; override with set_vcs_per_vnet() before build()
}

Topology::~Topology()
{
    for (auto p : m_tgs)          delete p;
    for (auto p : m_nis)          delete p;
    for (auto p : m_routers)      delete p;
    for (auto p : m_links)        delete p;
    for (auto p : m_credit_links) delete p;
}

Topology* Topology::create(std::string name, GarnetNetwork* net,
                            int rows, int cols, int depth,
                            const TopologyParams& params)
{
    if (name == "Mesh_XY") {
        return new MeshTopology(net, rows, cols, depth);
    }

    if (name == "T4" || name == "Star") {
        return new StarTopology(net, params.num_cpus > 0 ? params.num_cpus : 16);
    }

    if (name == "PACE_Chiplet" || name == "PACE_Chiplet_CMesh") {
        return new ChipletTopology(net, params);
    }

    // Check if it is a config file
    if (name.length() > 5 && name.substr(name.length() - 5) == ".conf") {
        return new FileTopology(net, name);
    }

    // Check if it is a python topology file
    if (name.length() > 3 && name.substr(name.length() - 3) == ".py") {
        std::string conf_file = "topology.conf";

        char result[4096];
        ssize_t count = readlink("/proc/self/exe", result, 4096);
        std::string exe_path;
        if (count != -1) {
            exe_path = std::string(result, count);
            size_t last_slash = exe_path.find_last_of("/");
            if (last_slash != std::string::npos)
                exe_path = exe_path.substr(0, last_slash);
        } else {
            exe_path = "garnet_standalone";
        }

        std::string script_path = exe_path + "/python/conf_generator.py";
        std::string command = "python3 " + script_path + " --topology " + name +
                              " --rows " + std::to_string(rows) +
                              " --cols " + std::to_string(cols) +
                              " --num-cpus " + std::to_string(rows * cols * depth);

        std::cout << "Compiling Python topology..." << std::endl;
        int ret = system(command.c_str());
        if (ret != 0) {
            std::cerr << "Error: Failed to generate topology config from " << name << std::endl;
            exit(1);
        }
        return new FileTopology(net, conf_file);
    }

    std::cerr << "Unknown topology: " << name << ". Defaulting to Mesh_XY." << std::endl;
    return new MeshTopology(net, rows, cols, depth);
}

// ============================================================
// Topology shared helpers
// ============================================================

void Topology::connectRouters(int src, int dest, int link_id_base,
                              std::string src_out_dir, std::string dest_in_dir,
                              int latency)
{
    NetworkLink::Params link_p;
    link_p.id = link_id_base;
    link_p.latency = latency;
    link_p.virtual_networks = m_num_vns;
    link_p.net_ptr = m_net;
    NetworkLink* link = new NetworkLink(link_p);
    m_links.push_back(link);

    CreditLink::Params credit_p;
    credit_p.id = link_id_base + 1;
    credit_p.latency = 1;
    credit_p.virtual_networks = m_num_vns;
    credit_p.net_ptr = m_net;
    CreditLink* credit_link = new CreditLink(credit_p);
    m_credit_links.push_back(credit_link);

    std::vector<NetDest> routing_table_entry(m_num_vns);

    m_routers[src]->addOutPort(src_out_dir, link, routing_table_entry,
                               1, credit_link, m_vcs_per_vnet);
    m_routers[dest]->addInPort(dest_in_dir, link, credit_link);
}

void Topology::connectNiToRouter(int ni_id, int router_id, int link_id_base,
                                 const std::string& local_dir)
{
    // NI -> Router
    NetworkLink::Params l1_p;
    l1_p.id = link_id_base;
    l1_p.latency = 1;
    l1_p.virtual_networks = m_num_vns;
    l1_p.net_ptr = m_net;
    NetworkLink* ni_to_r = new NetworkLink(l1_p);
    m_links.push_back(ni_to_r);

    CreditLink::Params c1_p;
    c1_p.id = link_id_base + 1;
    c1_p.latency = 1;
    c1_p.virtual_networks = m_num_vns;
    c1_p.net_ptr = m_net;
    CreditLink* r_to_ni_credit = new CreditLink(c1_p);
    m_credit_links.push_back(r_to_ni_credit);

    m_nis[ni_id]->addOutPort(ni_to_r, r_to_ni_credit, router_id, m_vcs_per_vnet);
    m_routers[router_id]->addInPort(local_dir, ni_to_r, r_to_ni_credit);

    // Router -> NI
    NetworkLink::Params l2_p;
    l2_p.id = link_id_base + 2;
    l2_p.latency = 1;
    l2_p.virtual_networks = m_num_vns;
    l2_p.net_ptr = m_net;
    NetworkLink* r_to_ni = new NetworkLink(l2_p);
    m_links.push_back(r_to_ni);

    CreditLink::Params c2_p;
    c2_p.id = link_id_base + 3;
    c2_p.latency = 1;
    c2_p.virtual_networks = m_num_vns;
    c2_p.net_ptr = m_net;
    CreditLink* ni_to_r_credit = new CreditLink(c2_p);
    m_credit_links.push_back(ni_to_r_credit);

    std::vector<NetDest> routing_table_entry(m_num_vns);
    m_routers[router_id]->addOutPort(local_dir, r_to_ni, routing_table_entry,
                                     1, ni_to_r_credit, m_vcs_per_vnet);
    m_nis[ni_id]->addInPort(r_to_ni, ni_to_r_credit);
}

// ============================================================
// MeshTopology
// ============================================================

void MeshTopology::build()
{
    int num_routers = m_rows * m_cols;

    for (int i = 0; i < num_routers; ++i) {
        int x = i % m_cols;
        int y = i / m_cols;

        Router::Params router_p;
        router_p.id = i; router_p.x = x; router_p.y = y; router_p.z = 0;
        router_p.virtual_networks = m_num_vns;
        router_p.vcs_per_vnet = m_vcs_per_vnet;
        router_p.network_ptr = m_net;
        router_p.latency = 1;
        m_routers.push_back(new Router(router_p));
        m_net->registerRouter(m_routers.back());

        NetworkInterface::Params ni_p;
        ni_p.id = i; ni_p.x = x; ni_p.y = y; ni_p.z = 0;
        ni_p.virtual_networks = m_num_vns;
        ni_p.vcs_per_vnet = m_vcs_per_vnet;
        ni_p.deadlock_threshold = 50000;
        ni_p.net_ptr = m_net;
        m_nis.push_back(new NetworkInterface(ni_p));
        m_net->registerNI(m_nis.back());

        SimpleTrafficGenerator* tg = new SimpleTrafficGenerator(
            i, num_routers, 0.0, m_net, m_nis.back());
        m_tgs.push_back(tg);
        m_nis.back()->setTrafficGenerator(tg);
    }

    m_link_id_counter = 0;
    for (int i = 0; i < num_routers; ++i) {
        connectNiToRouter(i, i, m_link_id_counter);
        m_link_id_counter += 4;
    }

    for (int row = 0; row < m_rows; row++) {
        for (int col = 0; col < m_cols; col++) {
            int curr = row * m_cols + col;

            if (col < m_cols - 1) {
                int east = row * m_cols + (col + 1);
                connectRouters(curr, east, m_link_id_counter, "East", "West");
                m_link_id_counter += 2;
            }
            if (col > 0) {
                int west = row * m_cols + (col - 1);
                connectRouters(curr, west, m_link_id_counter, "West", "East");
                m_link_id_counter += 2;
            }
            if (row > 0) {
                int north = (row - 1) * m_cols + col;
                connectRouters(curr, north, m_link_id_counter, "North", "South");
                m_link_id_counter += 2;
            }
            if (row < m_rows - 1) {
                int south = (row + 1) * m_cols + col;
                connectRouters(curr, south, m_link_id_counter, "South", "North");
                m_link_id_counter += 2;
            }
        }
    }

    for (auto router : m_routers) {
        int my_x = router->get_x();
        int my_y = router->get_y();

        for (int dest_ni = 0; dest_ni < num_routers; dest_ni++) {
            int dx = dest_ni % m_cols;
            int dy = dest_ni / m_cols;

            std::string dir = "Local";
            if (dx > my_x)       dir = "East";
            else if (dx < my_x)  dir = "West";
            else if (dy > my_y)  dir = "South";
            else if (dy < my_y)  dir = "North";

            int port = router->getOutportIndex(dir);
            if (port != -1) router->addRouteForPort(port, dest_ni);
        }
    }
}

int MeshTopology::get_diameter() const
{
    return (m_rows > 0 ? m_rows - 1 : 0) + (m_cols > 0 ? m_cols - 1 : 0);
}

// ============================================================
// ChipletTopology
// ============================================================

ChipletTopology::ChipletTopology(GarnetNetwork* net, const TopologyParams& p)
    : Topology(net, p.intra_rows, p.intra_cols, 1), m_params(p)
{
    m_vcs_per_vnet = p.vcs_per_vnet;
}

void ChipletTopology::build()
{
    int n_chiplets  = m_params.num_chiplets;
    int n_intra     = m_params.intra_rows * m_params.intra_cols;
    int num_routers = n_chiplets * n_intra;

    // CMesh support: num_cpus may be larger than num_routers (multiple NIs per router).
    // concentration = NIs per router; 1 = standard (one NI per router).
    int num_cpus = (m_params.num_cpus > 0) ? m_params.num_cpus : num_routers;
    int concentration = num_cpus / num_routers;
    if (num_cpus % num_routers != 0) {
        std::cerr << "ChipletTopology: num_cpus (" << num_cpus
                  << ") must be a multiple of num_routers (" << num_routers
                  << "). Rounding down concentration.\n";
        concentration = num_cpus / num_routers;
        num_cpus = concentration * num_routers;
    }

    // Adjacency list for Dijkstra routing.
    // adj[r] = list of {dst_router, latency_cycles, outport_direction_at_r}
    struct AdjEdge { int dst; int lat; std::string dir; };
    std::vector<std::vector<AdjEdge>> adj(num_routers);

    // ---- 1. Create routers ----
    for (int r = 0; r < num_routers; ++r) {
        int chiplet  = r / n_intra;
        int local_id = r % n_intra;
        int lx = local_id % m_params.intra_cols;
        int ly = local_id / m_params.intra_cols;
        int gx = chiplet * m_params.intra_cols + lx;
        int gy = ly;

        Router::Params rp;
        rp.id = r; rp.x = gx; rp.y = gy; rp.z = 0;
        rp.virtual_networks = m_num_vns;
        rp.vcs_per_vnet     = m_vcs_per_vnet;
        rp.network_ptr      = m_net;
        rp.latency          = 1;
        m_routers.push_back(new Router(rp));
        m_net->registerRouter(m_routers.back());
    }

    // ---- 1b. Create NIs (num_cpus total; concentration per router for CMesh) ----
    for (int i = 0; i < num_cpus; ++i) {
        int router_id = i / concentration;
        int chiplet   = router_id / n_intra;
        int local_id  = router_id % n_intra;
        int lx = local_id % m_params.intra_cols;
        int ly = local_id / m_params.intra_cols;
        int gx = chiplet * m_params.intra_cols + lx;
        int gy = ly;

        NetworkInterface::Params np;
        np.id = i; np.x = gx; np.y = gy; np.z = 0;
        np.virtual_networks    = m_num_vns;
        np.vcs_per_vnet        = m_vcs_per_vnet;
        np.deadlock_threshold  = 50000;
        np.net_ptr             = m_net;
        m_nis.push_back(new NetworkInterface(np));
        m_net->registerNI(m_nis.back());

        SimpleTrafficGenerator* tg = new SimpleTrafficGenerator(
            i, num_cpus, 0.0, m_net, m_nis.back());
        m_tgs.push_back(tg);
        m_nis.back()->setTrafficGenerator(tg);
    }

    // ---- 2. NI <-> Router local links ----
    // For concentration > 1: each router gets ports "Local_0", "Local_1", ...
    // For concentration == 1: use "Local" (backward compatible).
    m_link_id_counter = 0;
    for (int i = 0; i < num_cpus; ++i) {
        int router_id = i / concentration;
        std::string local_dir = (concentration > 1)
                                ? "Local_" + std::to_string(i % concentration)
                                : "Local";
        connectNiToRouter(i, router_id, m_link_id_counter, local_dir);
        m_link_id_counter += 4;
    }

    // ---- 3. Intra-chiplet mesh links ----
    for (int c = 0; c < n_chiplets; ++c) {
        int base = c * n_intra;
        for (int row = 0; row < m_params.intra_rows; ++row) {
            for (int col = 0; col < m_params.intra_cols; ++col) {
                int cur = base + row * m_params.intra_cols + col;

                if (col + 1 < m_params.intra_cols) {
                    int east = base + row * m_params.intra_cols + (col + 1);
                    connectRouters(cur, east, m_link_id_counter, "East", "West");
                    m_link_id_counter += 2;
                    adj[cur].push_back({east, 1, "East"});
                }
                if (col > 0) {
                    int west = base + row * m_params.intra_cols + (col - 1);
                    connectRouters(cur, west, m_link_id_counter, "West", "East");
                    m_link_id_counter += 2;
                    adj[cur].push_back({west, 1, "West"});
                }
                if (row + 1 < m_params.intra_rows) {
                    int south = base + (row + 1) * m_params.intra_cols + col;
                    connectRouters(cur, south, m_link_id_counter, "South", "North");
                    m_link_id_counter += 2;
                    adj[cur].push_back({south, 1, "South"});
                }
                if (row > 0) {
                    int north = base + (row - 1) * m_params.intra_cols + col;
                    connectRouters(cur, north, m_link_id_counter, "North", "South");
                    m_link_id_counter += 2;
                    adj[cur].push_back({north, 1, "North"});
                }
            }
        }
    }

    // ---- 4. Inter-chiplet links ----
    // Gateway of chiplet c = router c * n_intra (local position 0,0 in each chiplet).
    auto gw = [&](int c) { return c * n_intra; };

    // Add a bidirectional inter-chiplet link between chiplet ca and chiplet cb.
    // Uses unique direction names based on the peer chiplet id.
    // Returns true if added, false if pair already exists.
    std::set<std::pair<int,int>> added_pairs;
    auto add_inter = [&](int ca, int cb) {
        // Normalise key so (a,b) and (b,a) map to the same entry
        std::pair<int,int> key(std::min(ca,cb), std::max(ca,cb));
        if (added_pairs.count(key)) return;
        added_pairs.insert(key);

        int ra = gw(ca), rb = gw(cb);
        std::string a_to_b_dir = "ExtTo" + std::to_string(cb);
        std::string b_to_a_dir = "ExtTo" + std::to_string(ca);

        // ca -> cb
        connectRouters(ra, rb, m_link_id_counter,
                       a_to_b_dir, "ExtFrom" + std::to_string(ca),
                       m_params.inter_latency);
        m_link_id_counter += 2;
        adj[ra].push_back({rb, m_params.inter_latency, a_to_b_dir});

        // cb -> ca
        connectRouters(rb, ra, m_link_id_counter,
                       b_to_a_dir, "ExtFrom" + std::to_string(cb),
                       m_params.inter_latency);
        m_link_id_counter += 2;
        adj[rb].push_back({ra, m_params.inter_latency, b_to_a_dir});
    };

    if (m_params.inter_topology == "ring") {
        // Connect each chiplet to its two ring neighbors.
        for (int c = 0; c < n_chiplets; ++c)
            add_inter(c, (c + 1) % n_chiplets);
    } else if (m_params.inter_topology == "mesh") {
        // Arrange chiplets in a near-square 2D grid.
        int mc = 1, mr = n_chiplets;
        for (int f = 1; f * f <= n_chiplets; ++f)
            if (n_chiplets % f == 0) { mr = f; mc = n_chiplets / f; }
        for (int r = 0; r < mr; ++r) {
            for (int c = 0; c < mc; ++c) {
                int id = r * mc + c;
                if (c + 1 < mc) add_inter(id, r * mc + c + 1);
                if (r + 1 < mr) add_inter(id, (r + 1) * mc + c);
            }
        }
    } else if (m_params.inter_topology == "fc") {
        for (int a = 0; a < n_chiplets; ++a)
            for (int b = a + 1; b < n_chiplets; ++b)
                add_inter(a, b);
    } else {
        // "bus": star topology through chiplet 0's gateway.
        for (int c = 1; c < n_chiplets; ++c)
            add_inter(0, c);
    }

    std::cout << "ChipletTopology: " << n_chiplets << " chiplets x "
              << m_params.intra_rows << "x" << m_params.intra_cols
              << " intra-mesh, inter=" << m_params.inter_topology
              << " lat=" << m_params.inter_latency << "\n";

    // ---- 5. Dijkstra routing from each source router ----
    // For each src router, find the first-hop outport direction to every dst.
    // Populate routing table: router[src].addRouteForPort(port_idx, dest_ni).

    typedef std::pair<int,int> PQEntry; // {distance, router_id}

    int max_dist = 0;

    for (int src = 0; src < num_routers; ++src) {
        std::vector<int> dist(num_routers, INT_MAX);
        std::vector<std::string> first_hop(num_routers, "");
        dist[src] = 0;

        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<PQEntry>> pq;
        pq.push(PQEntry(0, src));

        while (!pq.empty()) {
            PQEntry top = pq.top(); pq.pop();
            int d = top.first, u = top.second;
            if (d > dist[u]) continue; // stale entry

            for (int ei = 0; ei < (int)adj[u].size(); ++ei) {
                const AdjEdge& e = adj[u][ei];
                int nd = d + e.lat;
                if (nd < dist[e.dst]) {
                    dist[e.dst] = nd;
                    // Track the first-hop outport from src:
                    // If we're at src itself, the first hop is e.dir.
                    // Otherwise inherit the first hop that led us to u.
                    first_hop[e.dst] = (u == src) ? e.dir : first_hop[u];
                    pq.push(PQEntry(nd, e.dst));
                }
            }
        }

        for (int d : dist) {
            if (d != INT_MAX && d > max_dist) max_dist = d;
        }

        // Populate routing table for this source router.
        // dest_ni runs over all num_cpus NIs; dest_router = dest_ni / concentration.
        for (int dest_ni = 0; dest_ni < num_cpus; ++dest_ni) {
            int dest_router = dest_ni / concentration;
            if (dest_router == src) {
                // Locally attached NI: route to its specific local port.
                std::string local_dir = (concentration > 1)
                                        ? "Local_" + std::to_string(dest_ni % concentration)
                                        : "Local";
                int port = m_routers[src]->getOutportIndex(local_dir);
                if (port >= 0) m_routers[src]->addRouteForPort(port, dest_ni);
            } else if (!first_hop[dest_router].empty()) {
                int port = m_routers[src]->getOutportIndex(first_hop[dest_router]);
                if (port >= 0) m_routers[src]->addRouteForPort(port, dest_ni);
            } else {
                std::cerr << "ChipletTopology: WARNING: no route from router "
                          << src << " to NI " << dest_ni
                          << " (dest_router=" << dest_router << ")\n";
            }
        }
    }
    m_diameter = max_dist;
}

int ChipletTopology::get_diameter() const
{
    return m_diameter;
}

// ============================================================
// StarTopology implementation
// ============================================================

void StarTopology::build()
{
    // num_routers = m_num_nodes
    int num_routers = m_num_nodes;
    for (int i = 0; i < num_routers; ++i) {
        GarnetRouterParams rp;
        rp.id = i;
        rp.x = i; rp.y = 0; rp.z = 0; // dummy coordinates
        rp.virtual_networks = m_num_vns;
        rp.vcs_per_vnet = m_vcs_per_vnet;
        rp.latency = 1;
        rp.network_ptr = m_net;
        m_routers.push_back(new Router(rp));

        NetworkInterface::Params ni_p;
        ni_p.id = i;
        ni_p.x = i; ni_p.y = 0; ni_p.z = 0;
        ni_p.virtual_networks = m_num_vns;
        ni_p.vcs_per_vnet = m_vcs_per_vnet;
        ni_p.deadlock_threshold = 1000;
        ni_p.net_ptr = m_net;
        m_nis.push_back(new NetworkInterface(ni_p));
    }

    // Connect NIs to Routers
    for (int i = 0; i < num_routers; ++i) {
        connectNiToRouter(i, i, m_link_id_counter);
        m_link_id_counter += 4;
    }

    // Connect leaf routers (1..num_routers-1) to center router (0)
    for (int i = 1; i < num_routers; ++i) {
        // leaf -> center
        connectRouters(i, 0, m_link_id_counter, "ToCenter", "FromLeaf" + std::to_string(i));
        m_link_id_counter += 2;
        // center -> leaf
        connectRouters(0, i, m_link_id_counter, "ToLeaf" + std::to_string(i), "FromCenter");
        m_link_id_counter += 2;
    }

    // Dijkstra routing
    for (int src = 0; src < num_routers; ++src) {
        for (int dest_ni = 0; dest_ni < num_routers; ++dest_ni) {
            int dest_router = dest_ni;
            if (src == dest_router) {
                int port = m_routers[src]->getOutportIndex("Local");
                m_routers[src]->addRouteForPort(port, dest_ni);
            } else if (src == 0) {
                // center -> leaf
                int port = m_routers[src]->getOutportIndex("ToLeaf" + std::to_string(dest_router));
                m_routers[src]->addRouteForPort(port, dest_ni);
            } else if (dest_router == 0) {
                // leaf -> center
                int port = m_routers[src]->getOutportIndex("ToCenter");
                m_routers[src]->addRouteForPort(port, dest_ni);
            } else {
                // leaf -> center -> leaf
                int port = m_routers[src]->getOutportIndex("ToCenter");
                m_routers[src]->addRouteForPort(port, dest_ni);
            }
        }
    }
}

} // namespace garnet

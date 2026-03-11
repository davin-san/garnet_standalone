#include "FileTopology.hh"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>
#include <queue>
#include <algorithm>
#include <map>

namespace garnet {

FileTopology::FileTopology(GarnetNetwork* net, std::string filename)
    : Topology(net, 0, 0, 1), m_filename(filename)
{
}

void FileTopology::build() {
    std::ifstream fin(m_filename);
    if (!fin.is_open()) {
        std::cerr << "Error: Could not open topology file: " << m_filename << std::endl;
        exit(1);
    }

    int num_routers = 0;
    int num_nis = 0;

    std::string line;
    enum ParseState { HEADER, EXT_LINKS, INT_LINKS, ROUTING_TABLES };
    ParseState state = HEADER;

    std::map<int, std::vector<std::pair<int, int>>> adj;

    while (std::getline(fin, line)) {
        if (line.empty() || line[0] == '#') continue;
        
        std::stringstream ss(line);
        std::string first;
        ss >> first;

        if (first == "NumRouters") {
            ss >> num_routers;
            for (int i = 0; i < num_routers; ++i) {
                std::getline(fin, line);
                std::stringstream rss(line);
                int id, x, y, z;
                rss >> id >> x >> y >> z;
                Router::Params p;
                p.id = id;
                p.x = x;
                p.y = y;
                p.z = z;
                p.virtual_networks = m_num_vns;
                p.vcs_per_vnet = m_vcs_per_vnet;
                p.network_ptr = m_net;
                p.latency = 1;
                m_routers.push_back(new Router(p));
                m_net->registerRouter(m_routers.back());
            }
        } else if (first == "NumNIs") {
            ss >> num_nis;
            for (int i = 0; i < num_nis; ++i) {
                std::getline(fin, line);
                std::stringstream nss(line);
                int id, x, y, z;
                nss >> id >> x >> y >> z;
                NetworkInterface::Params p;
                p.id = id;
                p.x = x;
                p.y = y;
                p.z = z;
                p.virtual_networks = m_num_vns;
                p.vcs_per_vnet = m_vcs_per_vnet;
                p.deadlock_threshold = 50000;
                p.net_ptr = m_net;
                m_nis.push_back(new NetworkInterface(p));
                m_net->registerNI(m_nis.back());

                SimpleTrafficGenerator* tg = new SimpleTrafficGenerator(
                    id, num_nis, 0.0, m_net, m_nis.back()
                );
                m_tgs.push_back(tg);
                m_nis.back()->setTrafficGenerator(tg);
            }
        } else if (first == "ExtLinks") {
            state = EXT_LINKS;
            m_link_id_counter = 0;
        } else if (first == "IntLinks") {
            state = INT_LINKS;
        } else if (first == "RoutingTables") {
            state = ROUTING_TABLES;
        } else {
            // Data line
            std::stringstream data_ss(line);
            if (state == EXT_LINKS) {
                int ni_id, router_id;
                if (data_ss >> ni_id >> router_id) {
                    connectNiToRouter(ni_id, router_id, m_link_id_counter);
                    m_link_id_counter += 4;
                }
            } else if (state == INT_LINKS) {
                int src, dst, lat, weight;
                std::string src_p, dst_p;
                if (data_ss >> src >> dst >> lat >> weight >> src_p >> dst_p) {
                    connectRouters(src, dst, m_link_id_counter, src_p, dst_p, lat);
                    m_link_id_counter += 2;
                    adj[src].push_back({dst, lat});
                }
            } else if (state == ROUTING_TABLES) {
                int r_id, dest_ni, port;
                if (data_ss >> r_id >> dest_ni >> port) {
                    m_routers[r_id]->addRouteForPort(port, dest_ni);
                }
            }
        }
    }

    fin.close();

    // Calculate diameter using Dijkstra (since lat can be > 1)
    int max_dist = 0;
    for (auto const& r_start : m_routers) {
        int src = r_start->get_id();
        std::map<int, int> dists;
        for (auto const& r : m_routers) dists[r->get_id()] = 1000000;
        dists[src] = 0;

        typedef std::pair<int, int> ipair;
        std::priority_queue<ipair, std::vector<ipair>, std::greater<ipair>> pq;
        pq.push({0, src});

        while (!pq.empty()) {
            int d = pq.top().first;
            int u = pq.top().second;
            pq.pop();

            if (d > dists[u]) continue;

            if (adj.count(u)) {
                for (auto const& edge : adj.at(u)) {
                    int v = edge.first;
                    int weight = edge.second;
                    if (dists[v] > dists[u] + weight) {
                        dists[v] = dists[u] + weight;
                        pq.push({dists[v], v});
                    }
                }
            }
        }

        for (auto const& d : dists) {
            if (d.second != 1000000 && d.second > max_dist) max_dist = d.second;
        }
    }
    m_diameter = max_dist;
}

} // namespace garnet

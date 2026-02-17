#include "FileTopology.hh"
#include <fstream>
#include <iostream>
#include <sstream>
#include <cassert>

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
}

} // namespace garnet

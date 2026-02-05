#include "Topology.hh"
#include "FileTopology.hh"
#include <iostream>
#include <unistd.h> // For readlink

namespace garnet {

Topology::Topology(GarnetNetwork* net, int num_rows, int num_cols)
    : m_net(net), m_rows(num_rows), m_cols(num_cols), m_link_id_counter(0)
{
    // These could be passed in or read from net params
    m_num_vns = 2; // Default
    m_vcs_per_vnet = 4; // Default
}

Topology::~Topology()
{
    for (auto p : m_tgs) delete p;
    for (auto p : m_nis) delete p;
    for (auto p : m_routers) delete p;
    for (auto p : m_links) delete p;
    for (auto p : m_credit_links) delete p;
}

Topology* Topology::create(std::string name, GarnetNetwork* net, int rows, int cols)
{
    if (name == "Mesh_XY") {
        return new MeshTopology(net, rows, cols);
    }
    
    // Check if it is a config file
    if (name.length() > 5 && name.substr(name.length() - 5) == ".conf") {
        return new FileTopology(net, name);
    }
    
    // Check if it is a python topology file
    if (name.length() > 3 && name.substr(name.length() - 3) == ".py") {
        std::string conf_file = "topology.conf";
        
        // Find path to executable to locate python script relative to it
        char result[4096];
        ssize_t count = readlink("/proc/self/exe", result, 4096);
        std::string exe_path;
        if (count != -1) {
            exe_path = std::string(result, count);
            // Remove executable name to get directory
            size_t last_slash = exe_path.find_last_of("/");
            if (last_slash != std::string::npos) {
                exe_path = exe_path.substr(0, last_slash);
            }
        } else {
            // Fallback
            exe_path = "garnet_standalone"; 
        }

        // Construct path to conf_generator.py
        // Assumes directory structure: 
        //   [Exe Dir]
        //      |-- garnet_standalone (binary)
        //      |-- python/
        //            |-- conf_generator.py
        
        // The binary is in 'garnet_standalone/' usually. 
        // If we run from root: ./garnet_standalone/garnet_standalone
        // Exe path is .../garnet_standalone
        // Python is .../garnet_standalone/python/conf_generator.py
        
        std::string script_path = exe_path + "/python/conf_generator.py";
        
        std::string command = "python3 " + script_path + " --topology " + name + 
                              " --rows " + std::to_string(rows) + 
                              " --cols " + std::to_string(cols);
        
        std::cout << "Compiling Python topology..." << std::endl;
        int ret = system(command.c_str());
        if (ret != 0) {
            std::cerr << "Error: Failed to generate topology config from " << name << std::endl;
            exit(1);
        }
        return new FileTopology(net, conf_file);
    }

    // Future: Add "Mesh_3D", "Torus", etc. here
    std::cerr << "Unknown topology: " << name << ". Defaulting to Mesh_XY." << std::endl;
    return new MeshTopology(net, rows, cols);
}

void Topology::connectRouters(int src, int dest, int link_id_base, 
                              std::string src_out_dir, std::string dest_in_dir,
                              int latency)
{
    // Data Link: Src -> Dest
    NetworkLink::Params link_p;
    link_p.id = link_id_base;
    link_p.latency = latency;
    link_p.virtual_networks = m_num_vns;
    link_p.net_ptr = m_net;
    NetworkLink* link = new NetworkLink(link_p);
    m_links.push_back(link);

    // Credit Link: Dest -> Src
    CreditLink::Params credit_p;
    credit_p.id = link_id_base + 1;
    credit_p.latency = 1;
    credit_p.virtual_networks = m_num_vns;
    credit_p.net_ptr = m_net;
    CreditLink* credit_link = new CreditLink(credit_p);
    m_credit_links.push_back(credit_link);

    // Routing entry (not strictly used for XY but required by signature)
    std::vector<NetDest> routing_table_entry(m_num_vns);

    // Connect Source Output
    m_routers[src]->addOutPort(src_out_dir, link, routing_table_entry, 
                               1, credit_link, m_vcs_per_vnet);

    // Connect Dest Input
    m_routers[dest]->addInPort(dest_in_dir, link, credit_link);
}

void Topology::connectNiToRouter(int ni_id, int router_id, int link_id_base)
{
    // NI -> Router (Link ID: base)
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
    m_routers[router_id]->addInPort("Local", ni_to_r, r_to_ni_credit);

    // Router -> NI (Link ID: base + 2)
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
    m_routers[router_id]->addOutPort("Local", r_to_ni, routing_table_entry, 
                                     1, ni_to_r_credit, m_vcs_per_vnet);
    m_nis[ni_id]->addInPort(r_to_ni, ni_to_r_credit);
}

void MeshTopology::build()
{
    int num_routers = m_rows * m_cols;
    
    // 1. Create Routers and NIs
    for (int i = 0; i < num_routers; ++i) {
        // Router
        Router::Params router_p;
        router_p.id = i;
        router_p.virtual_networks = m_num_vns;
        router_p.vcs_per_vnet = m_vcs_per_vnet;
        router_p.network_ptr = m_net;
        router_p.latency = 1;
        m_routers.push_back(new Router(router_p));

        // Network Interface
        NetworkInterface::Params ni_p;
        ni_p.id = i;
        ni_p.virtual_networks = m_num_vns;
        ni_p.vcs_per_vnet = m_vcs_per_vnet;
        ni_p.deadlock_threshold = 50000;
        ni_p.net_ptr = m_net;
        m_nis.push_back(new NetworkInterface(ni_p));
        m_net->registerNI(m_nis.back());

        // Traffic Generator (Attached to NI)
        // Note: Using default params for now, can be parameterized later
        SimpleTrafficGenerator* tg = new SimpleTrafficGenerator(
            i, num_routers, 0.0, m_net, m_nis.back() // 0.0 rate, main.cc will set active
        );
        m_tgs.push_back(tg);
        m_nis.back()->setTrafficGenerator(tg);
    }

    // 2. Connect NI <-> Router (Local Links)
    m_link_id_counter = 0;
    for (int i = 0; i < num_routers; ++i) {
        connectNiToRouter(i, i, m_link_id_counter);
        m_link_id_counter += 4;
    }

    // 3. Connect Routers (Mesh Links)
    // Loop over rows and cols
    for (int col = 0; col < m_cols; col++) {
        for (int row = 0; row < m_rows; row++) {
            int curr = row * m_cols + col;

            // South Neighbor (row + 1)
            if (row < m_rows - 1) {
                int south = (row + 1) * m_cols + col;
                
                // Curr -> South
                connectRouters(curr, south, m_link_id_counter, "South", "North");
                m_link_id_counter += 2;
                
                // South -> Curr
                connectRouters(south, curr, m_link_id_counter, "North", "South");
                m_link_id_counter += 2;
            }

            // East Neighbor (col + 1)
            if (col < m_cols - 1) {
                int east = row * m_cols + (col + 1);
                
                // Curr -> East
                connectRouters(curr, east, m_link_id_counter, "East", "West");
                m_link_id_counter += 2;

                // East -> Curr
                connectRouters(east, curr, m_link_id_counter, "West", "East");
                m_link_id_counter += 2;
            }
        }
    }
}

} // namespace garnet
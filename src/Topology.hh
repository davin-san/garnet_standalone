#ifndef __GARNET_STANDALONE_TOPOLOGY_HH__
#define __GARNET_STANDALONE_TOPOLOGY_HH__

#include <vector>
#include <string>
#include "GarnetNetwork.hh"
#include "Router.hh"
#include "NetworkInterface.hh"
#include "NetworkLink.hh"
#include "CreditLink.hh"
#include "SimpleTrafficGenerator.hh"

namespace garnet {

class Topology {
public:
    Topology(GarnetNetwork* net, int num_rows, int num_cols);
    virtual ~Topology();

    // The main method to build the network
    virtual void build() = 0;

    // Accessors for simulation loop
    const std::vector<Router*>& getRouters() const { return m_routers; }
    const std::vector<NetworkInterface*>& getNIs() const { return m_nis; }
    const std::vector<SimpleTrafficGenerator*>& getTGs() const { return m_tgs; }

    // Factory method
    static Topology* create(std::string name, GarnetNetwork* net, int rows, int cols);

protected:
    // Helper to connect two routers
    void connectRouters(int src_id, int dest_id, int link_id_base, 
                        std::string src_out_dir, std::string dest_in_dir);

    // Helper to connect NI to Router
    void connectNiToRouter(int ni_id, int router_id, int link_id_base);

    GarnetNetwork* m_net;
    int m_rows;
    int m_cols;
    int m_num_vns;
    int m_vcs_per_vnet;

    // Components owned by Topology
    std::vector<Router*> m_routers;
    std::vector<NetworkInterface*> m_nis;
    std::vector<SimpleTrafficGenerator*> m_tgs;
    std::vector<NetworkLink*> m_links;
    std::vector<CreditLink*> m_credit_links;
    
    // Link ID counter to ensure uniqueness
    int m_link_id_counter;
};

class MeshTopology : public Topology {
public:
    MeshTopology(GarnetNetwork* net, int rows, int cols) 
        : Topology(net, rows, cols) {}
    
    void build() override;
};

} // namespace garnet

#endif // __GARNET_STANDALONE_TOPOLOGY_HH__

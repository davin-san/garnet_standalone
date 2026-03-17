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

// Parameters for topology creation (chiplet-specific params ignored for Mesh_XY).
struct TopologyParams {
    int num_chiplets        = 1;
    int intra_rows          = 4;
    int intra_cols          = 4;
    std::string inter_topology = "ring"; // ring, mesh, fc, bus
    int inter_latency       = 1;
    int inter_width         = 128;       // bits (informational; not modeled in flit sim)
    int vcs_per_vnet        = 4;
    // CMesh support: total NIs to create. 0 = one NI per router (default).
    // Set to num_cpus when num_cpus > num_routers for concentrated mesh.
    int num_cpus            = 0;
};

class Topology {
public:
    Topology(GarnetNetwork* net, int num_rows, int num_cols, int num_depth);
    virtual ~Topology();

    // The main method to build the network
    virtual void build() = 0;

    // Accessors for simulation loop
    const std::vector<Router*>& getRouters() const { return m_routers; }
    const std::vector<NetworkInterface*>& getNIs() const { return m_nis; }
    const std::vector<SimpleTrafficGenerator*>& getTGs() const { return m_tgs; }
    const std::vector<NetworkLink*>& getLinks() const { return m_links; }

    virtual int get_diameter() const = 0;

    // Factory method (accepts optional chiplet params)
    static Topology* create(std::string name, GarnetNetwork* net,
                            int rows, int cols, int depth,
                            const TopologyParams& params = TopologyParams());

    // Override default vnet count before calling build().
    void set_num_vnets(int n) { m_num_vns = n; }

    // Override VCs per vnet before calling build().
    void set_vcs_per_vnet(int n) { m_vcs_per_vnet = n; }

protected:
    // Helper to connect two routers (unidirectional)
    void connectRouters(int src_id, int dest_id, int link_id_base,
                        std::string src_out_dir, std::string dest_in_dir,
                        int latency = 1);

    // Helper to connect NI to Router.
    // local_dir: direction name used for the local NI port on the router.
    // Default "Local" for standard topologies; use "Local_N" for CMesh
    // concentration (multiple NIs per router) to keep port names unique.
    void connectNiToRouter(int ni_id, int router_id, int link_id_base,
                           const std::string& local_dir = "Local");

    GarnetNetwork* m_net;
    int m_rows;
    int m_cols;
    int m_depth;
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
    MeshTopology(GarnetNetwork* net, int rows, int cols, int depth)
        : Topology(net, rows, cols, depth) {}

    void build() override;
    int get_diameter() const override;
};

class ChipletTopology : public Topology {
public:
    ChipletTopology(GarnetNetwork* net, const TopologyParams& p);
    void build() override;
    int get_diameter() const override;
private:
    TopologyParams m_params;
    int m_diameter = 0;
};

} // namespace garnet

#endif // __GARNET_STANDALONE_TOPOLOGY_HH__

/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * ... (copyright header) ...
 */


#include "RoutingUnit.hh"

#include "InputUnit.hh"
#include "Router.hh"
#include "NetDest.hh"
#include "GarnetNetwork.hh"

#include <cstdlib>
#include <algorithm>

namespace garnet
{

RoutingUnit::RoutingUnit(Router *router)
{
    m_router = router;
    m_routing_table.clear();
    m_weight_table.clear();
}

void
RoutingUnit::addRoute(std::vector<NetDest>& routing_table_entry)
{
    if (routing_table_entry.size() > m_routing_table.size()) {
        m_routing_table.resize(routing_table_entry.size());
    }
    for (int v = 0; v < (int)routing_table_entry.size(); v++) {
        m_routing_table[v].push_back(routing_table_entry[v]);
    }
}

void
RoutingUnit::addWeight(int link_weight)
{
    m_weight_table.push_back(link_weight);
}

void
RoutingUnit::addRouteForPort(int port, int dest_ni)
{
    int num_vnets = m_router->get_num_vnets();
    if ((int)m_routing_table.size() < num_vnets) {
        m_routing_table.resize(num_vnets);
    }

    for (int v = 0; v < num_vnets; v++) {
        if ((int)m_routing_table[v].size() <= port) {
            m_routing_table[v].resize(port + 1);
        }
        m_routing_table[v][port].add(dest_ni);
    }
    
    if ((int)m_weight_table.size() <= port) {
        m_weight_table.resize(port + 1, 1);
    }
}

bool
RoutingUnit::supportsVnet(int vnet, std::vector<int> sVnets)
{
    if (sVnets.size() == 0) return true;
    if (std::find(sVnets.begin(), sVnets.end(), vnet) != sVnets.end()) return true;
    return false;
}

int
RoutingUnit::lookupRoutingTable(int vnet, const NetDest& msg_destination)
{
    int min_weight = -1; 
    std::vector<int> output_link_candidates;

    if (vnet >= (int)m_routing_table.size()) return -1;

    for (int link = 0; link < (int)m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[vnet][link])) {
            int weight = 1;
            if (link < (int)m_weight_table.size()) weight = m_weight_table[link];
            if (weight <= min_weight || min_weight == -1) min_weight = weight;
        }
    }

    for (int link = 0; link < (int)m_routing_table[vnet].size(); link++) {
        if (msg_destination.intersectionIsNotEmpty(m_routing_table[vnet][link])) {
            int weight = 1;
            if (link < (int)m_weight_table.size()) weight = m_weight_table[link];
            if (weight == min_weight) output_link_candidates.push_back(link);
        }
    }

    if (output_link_candidates.size() == 0) return -1;
    return output_link_candidates.at(0);
}


void
RoutingUnit::addInDirection(PortDirection inport_dirn, int inport_idx)
{
    m_inports_dirn2idx[inport_dirn] = inport_idx;
    m_inports_idx2dirn[inport_idx]  = inport_dirn;
}

void
RoutingUnit::addOutDirection(PortDirection outport_dirn, int outport_idx)
{
    m_outports_dirn2idx[outport_dirn] = outport_idx;
    m_outports_idx2dirn[outport_idx]  = outport_dirn;
}

int
RoutingUnit::outportCompute(RouteInfo route, int inport,
                            PortDirection inport_dirn)
{
    int outport = -1;
    if (m_router->get_net_ptr()->getRoutingAlgorithm() == 1) { // XY
        outport = outportComputeXY(route, inport, inport_dirn);
    } 
    
    // Fallback if XY routing is disabled or fails
    if (outport == -1) {
         outport = lookupRoutingTable(route.vnet, route.net_dest);
    }

    return outport;
}

int
RoutingUnit::outportComputeXY(RouteInfo route,
                              int inport,
                              PortDirection inport_dirn)
{
    PortDirection outport_dirn = "Unknown";

    int my_x = m_router->get_x();
    int my_y = m_router->get_y();
    int my_z = m_router->get_z();

    // We need the destination router's coordinates.
    // In standalone, we can get the router from the network if we know its ID.
    // For now, assume NI ID maps to Router ID for simplicity in 2x2 mesh
    int dest_id = route.dest_router;
    
    // This is a hack for now, but let's try to get it from the topology if possible
    // or just use the same mapping as NI if applicable.
    // In MeshTopology::build, NI i is connected to Router i.
    
    // For a real implementation, we should store all router coordinates in GarnetNetwork.
    // Let's assume for now that Router ID = x + y*cols + z*rows*cols
    int num_cols = m_router->get_net_ptr()->getNumCols();
    int num_rows = m_router->get_net_ptr()->getNumRows();

    int dest_x = dest_id % num_cols;
    int dest_y = (dest_id / num_cols) % num_rows;
    int dest_z = dest_id / (num_cols * num_rows);

    if (dest_x != my_x) {
        outport_dirn = (dest_x > my_x) ? "East" : "West";
    } else if (dest_y != my_y) {
        outport_dirn = (dest_y > my_y) ? "South" : "North";
    } else if (dest_z != my_z) {
        outport_dirn = (dest_z > my_z) ? "Up" : "Down";
    } else {
        outport_dirn = "Local";
    }

    return m_outports_dirn2idx[outport_dirn];
}

int
RoutingUnit::outportComputeCustom(RouteInfo route,
                                 int inport,
                                 PortDirection inport_dirn)
{
    return -1;
}

} // namespace garnet

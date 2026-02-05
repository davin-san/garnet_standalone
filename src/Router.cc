#include "Router.hh"
#include "CreditLink.hh"
#include "GarnetNetwork.hh"
#include "InputUnit.hh"
#include "NetworkLink.hh"
#include "OutputUnit.hh"
#include "RoutingUnit.hh"
#include "SwitchAllocator.hh"
#include "CrossbarSwitch.hh"

namespace garnet
{

Router::Router(const Params &p)
  : m_id(p.id), m_latency(p.latency),
    m_virtual_networks(p.virtual_networks), m_vc_per_vnet(p.vcs_per_vnet),
    m_num_vcs(m_virtual_networks * m_vc_per_vnet),
    m_network_ptr(p.network_ptr)
{
    m_routing_unit = new RoutingUnit(this);
    m_sw_alloc = new SwitchAllocator(this);
    m_crossbar_switch = new CrossbarSwitch(this);
    m_input_unit.clear();
    m_output_unit.clear();
}

Router::~Router()
{
    delete m_routing_unit;
    delete m_sw_alloc;
    delete m_crossbar_switch;
}

void
Router::init()
{
    m_sw_alloc->init();
    m_crossbar_switch->init();
}

void
Router::wakeup()
{
    for (int inport = 0; inport < m_input_unit.size(); inport++) {
        m_input_unit[inport]->wakeup();
    }
    for (int outport = 0; outport < m_output_unit.size(); outport++) {
        m_output_unit[outport]->wakeup();
    }
    m_sw_alloc->wakeup();
    m_crossbar_switch->wakeup();
}

void
Router::addInPort(PortDirection inport_dirn,
                  NetworkLink *in_link, CreditLink *credit_link)
{
    int port_num = m_input_unit.size();
    InputUnit *input_unit = new InputUnit(port_num, inport_dirn, this);
    input_unit->set_in_link(in_link);
    input_unit->set_credit_link(credit_link);
    in_link->setLinkConsumer(this);
    in_link->setVcsPerVnet(get_vc_per_vnet());
    credit_link->setSourceQueue(input_unit->getCreditQueue());
    credit_link->setVcsPerVnet(get_vc_per_vnet());
    m_input_unit.push_back(std::shared_ptr<InputUnit>(input_unit));
    m_routing_unit->addInDirection(inport_dirn, port_num);
}

void
Router::addOutPort(PortDirection outport_dirn,
                   NetworkLink *out_link,
                   std::vector<NetDest>& routing_table_entry, int link_weight,
                   CreditLink *credit_link, uint32_t consumerVcs)
{
    int port_num = m_output_unit.size();
    OutputUnit *output_unit = new OutputUnit(port_num, outport_dirn, this, consumerVcs);
    output_unit->set_out_link(out_link);
    output_unit->set_credit_link(credit_link);
    credit_link->setLinkConsumer(this);
    credit_link->setVcsPerVnet(consumerVcs);
    out_link->setSourceQueue(output_unit->getOutQueue());
    out_link->setVcsPerVnet(consumerVcs);
    m_output_unit.push_back(std::shared_ptr<OutputUnit>(output_unit));
    m_routing_unit->addRoute(routing_table_entry);
    m_routing_unit->addWeight(link_weight);
    m_routing_unit->addOutDirection(outport_dirn, port_num);
}

PortDirection Router::getOutportDirection(int outport) { return m_output_unit[outport]->get_direction(); }
PortDirection Router::getInportDirection(int inport) { return m_input_unit[inport]->get_direction(); }
int Router::route_compute(RouteInfo route, int inport, PortDirection inport_dirn) { return m_routing_unit->outportCompute(route, inport, inport_dirn); }
void Router::grant_switch(int inport, flit *t_flit) { m_crossbar_switch->update_sw_winner(inport, t_flit); }
std::string Router::getPortDirectionName(PortDirection direction) { return direction; }
void Router::scheduleEvent(uint64_t time) { m_network_ptr->getEventQueue()->schedule(this, time); }
void Router::addRouteForPort(int port, int dest_ni) { m_routing_unit->addRouteForPort(port, dest_ni); }

} // namespace garnet

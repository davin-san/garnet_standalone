#ifndef __GARNET_STANDALONE_FILE_TOPOLOGY_HH__
#define __GARNET_STANDALONE_FILE_TOPOLOGY_HH__

#include "Topology.hh"
#include <string>

namespace garnet {

class FileTopology : public Topology {
public:
    FileTopology(GarnetNetwork* net, std::string filename);
    void build() override;
    int get_diameter() const override { return m_diameter; }

private:
    std::string m_filename;
    int m_diameter = 0;
};

} // namespace garnet

#endif // __GARNET_STANDALONE_FILE_TOPOLOGY_HH__

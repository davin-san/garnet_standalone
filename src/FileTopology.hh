#ifndef __GARNET_STANDALONE_FILE_TOPOLOGY_HH__
#define __GARNET_STANDALONE_FILE_TOPOLOGY_HH__

#include "Topology.hh"
#include <string>

namespace garnet {

class FileTopology : public Topology {
public:
    FileTopology(GarnetNetwork* net, std::string filename);
    void build() override;

private:
    std::string m_filename;
};

} // namespace garnet

#endif // __GARNET_STANDALONE_FILE_TOPOLOGY_HH__

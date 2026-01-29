#ifndef __GARNET_SIM_OBJECT_HH__
#define __GARNET_SIM_OBJECT_HH__

#include <cstdint>

namespace garnet {

class GarnetSimObject {
public:
    GarnetSimObject() = default;
    virtual ~GarnetSimObject() = default;

    virtual void wakeup() = 0;
};

} // namespace garnet

#endif // __GARNET_SIM_OBJECT_HH__

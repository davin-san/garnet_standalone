#ifndef __GARNET_CONSUMER_HH__
#define __GARNET_CONSUMER_HH__

#include "GarnetSimObject.hh"

namespace garnet
{

class Consumer : public GarnetSimObject
{
  public:
    Consumer() = default;
    virtual ~Consumer() = default;

    virtual void scheduleEvent(uint64_t time) = 0;
};

} // namespace garnet

#endif // __GARNET_CONSUMER_HH__

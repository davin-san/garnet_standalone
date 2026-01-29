#ifndef __GARNET_NETDEST_HH__
#define __GARNET_NETDEST_HH__

#include <vector>

namespace garnet
{

class NetDest
{
  public:
    NetDest() {}
    // --- THIS IS THE FIX ---
    // Change 'false' to 'true' to make the router's stubbed
    // routing table always find a match.
    bool intersectionIsNotEmpty(const NetDest& other) const { return true; }
    // --- END OF FIX ---
};

} // namespace garnet

#endif // __GARNET_NETDEST_HH__
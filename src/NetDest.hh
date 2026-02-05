#ifndef __GARNET_NETDEST_HH__
#define __GARNET_NETDEST_HH__

#include <vector>
#include <algorithm>
#include <set>
#include <iostream>

namespace garnet
{

// A simplified NetDest using std::set for standalone simplicity
// In gem5 it's a bitset, but set is easier to drop in without extra deps.

class NetDest
{
  public:
    NetDest() {}
    
    void add(int dest_id) {
        m_destinations.insert(dest_id);
    }
    
    bool intersectionIsNotEmpty(const NetDest& other) const {
        const std::set<int>* small = &m_destinations;
        const std::set<int>* large = &other.m_destinations;
        
        if (large->size() < small->size()) {
            small = &other.m_destinations;
            large = &m_destinations;
        }
        
        for (int id : *small) {
            if (large->count(id)) return true;
        }
        return false;
    }
    
    void clear() { m_destinations.clear(); }

    void print() const {
        std::cout << "{";
        for (int id : m_destinations) std::cout << id << " ";
        std::cout << "}";
    }

  private:
    std::set<int> m_destinations;
};

} // namespace garnet

#endif // __GARNET_NETDEST_HH__
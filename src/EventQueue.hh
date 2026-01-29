#ifndef __GARNET_EVENT_QUEUE_HH__
#define __GARNET_EVENT_QUEUE_HH__

#include <cstdint>
#include <queue>
#include <vector>

#include "GarnetSimObject.hh"

namespace garnet {

class Event {
public:
    Event(GarnetSimObject* obj, uint64_t time) :
        m_obj(obj), m_time(time) {}

    GarnetSimObject* get_obj() const { return m_obj; }
    uint64_t get_time() const { return m_time; }

private:
    GarnetSimObject* m_obj;
    uint64_t m_time;
};

struct EventCompare {
    bool operator()(const Event* a, const Event* b) const {
        return a->get_time() > b->get_time();
    }
};

class EventQueue {
public:
    EventQueue() = default;
    ~EventQueue();

    void schedule(GarnetSimObject* obj, uint64_t time);
    Event* get_next_event();
    bool is_empty() const;
    uint64_t get_current_time() const;

private:
    std::priority_queue<Event*, std::vector<Event*>, EventCompare> m_event_queue;
    uint64_t m_current_time = 0;
};

} // namespace garnet

#endif // __GARNET_EVENT_QUEUE_HH__

#include "EventQueue.hh"

namespace garnet {

EventQueue::~EventQueue() {
    while (!m_event_queue.empty()) {
        delete m_event_queue.top();
        m_event_queue.pop();
    }
}

void
EventQueue::schedule(GarnetSimObject* obj, uint64_t time) {
    Event* event = new Event(obj, m_current_time + time);
    m_event_queue.push(event);
}

Event*
EventQueue::get_next_event() {
    if (m_event_queue.empty()) {
        return nullptr;
    }

    Event* event = m_event_queue.top();
    m_event_queue.pop();
    m_current_time = event->get_time();
    return event;
}

bool
EventQueue::is_empty() const {
    return m_event_queue.empty();
}

uint64_t
EventQueue::get_current_time() const {
    return m_current_time;
}

} // namespace garnet
#include "CreditLink.hh"
#include "Consumer.hh"

namespace garnet
{

CreditLink::CreditLink(const Params &p)
    : NetworkLink(NetworkLink::Params())
{
}

void
CreditLink::scheduleEvent(uint64_t time)
{
    link_consumer->scheduleEvent(time);
}

} // namespace garnet

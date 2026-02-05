/*
 * Copyright (c) 2008 Princeton University
 * Copyright (c) 2016 Georgia Institute of Technology
 * All rights reserved.
 *
 * ... (copyright header) ...
 */


#ifndef __GARNET_CREDIT_LINK_HH__
#define __GARNET_CREDIT_LINK_HH__

#include "NetworkLink.hh"

namespace garnet
{

struct CreditLinkParams : public NetworkLinkParams
{
};

class CreditLink : public NetworkLink
{
  public:
    typedef CreditLinkParams Params;
    CreditLink(const Params &p);
};

} // namespace garnet

#endif // __GARNET_CREDIT_LINK_HH__
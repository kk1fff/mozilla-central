/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsINetworkManager.h"
#include "nsINetworkInterfaceListService.h"

extern "C" {
#include "addrs.h"
#include "transport_addr.h"
}

namespace {
class InterfaceListUpdateHandler: public nsINetworkInterfaceListCallback
{
public:
  InterfaceListUpdateHandler()
    : mAddrs(nullptr)
    , mAddrLength(0)
  { }

  void OnInterfaceListGot(const nsINetworkInterfaceList& list)
  {
    
  }

  nr_transport_addr *mAddrs;
  int mAddrLength;
};

InterfaceListUpdateHandler gInterfaceListUpdateHandler;
} // namespace

void
updateNetworkInterfaceList()
{
  // Obtain network intefaces from network manager.
  nsCOMPtr<nsINetworkInterfaceListService> listService = do_GetService("@mozilla.org/network/interface-list-manager;1");
  listService.getInterfaceList(gInterfaceListUpdateHandler);


}

int
nr_stun_get_addrs(nr_transport_addr addrs[], int maxaddrs, int drop_loopback, int *count)
{
  // Return information of gInterfaceListUpdateHandler
}

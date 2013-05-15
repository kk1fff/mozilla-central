/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifdef MOZ_B2G_RIL

#include "nsINetworkManager.h"
#include "nsINetworkInterfaceListService.h"
#include "nsTArray.h"

extern "C" {
#include <arpa/inet.h>
#include <linux/if.h>
#include <net/if.h>
#include <net/route.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/param.h>
#include <sys/socket.h>
#include "r_types.h"
#include "csi_platform.h"
#include "stun.h"
#include "addrs.h"
}

#include "nsCOMPtr.h"
#include "nsThreadUtils.h"
#include "nsServiceManagerUtils.h"

namespace {

typedef struct {
  struct sockaddr_in addr;
  char name[64];
} NetworkInterface;

class GetNetworkInterfaceList: public nsRunnable {
public:
  nsresult Run()
  {
    // Obtain network intefaces from network manager.
    nsresult rv;
    nsCOMPtr<nsINetworkInterfaceList> networkList;
    nsCOMPtr<nsINetworkInterfaceListService> listService =
      do_GetService("@mozilla.org/network/interface-list-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    listService->GetDataInterfaceList(getter_AddRefs(networkList));

    // Translate nsINetworkInterfaceList to NetworkInterface.
    int32_t listLength;
    NS_ENSURE_SUCCESS(networkList->GetNumberOfInterface(&listLength), 1);
    mNetworkArray.SetLength(listLength);

    nsAutoString ip;
    nsAutoString ifaceName;
    for (int32_t i = 0; i < listLength; i++) {
      NetworkInterface &interface = mNetworkArray.ElementAt(i);
      nsCOMPtr<nsINetworkInterface> iface;
      rv = networkList->GetInterface(i, getter_AddRefs(iface));
      NS_ENSURE_SUCCESS(rv, rv);

      memset(&(interface.addr), 0, sizeof(interface.addr));
      interface.addr.sin_family = AF_INET;
      iface->GetIp(ip);
      inet_pton(AF_INET, NS_ConvertUTF16toUTF8(ip).get(),
                &(interface.addr.sin_addr.s_addr));

      iface->GetName(ifaceName);
      strncpy(interface.name,
              NS_ConvertUTF16toUTF8(ifaceName).get(),
              sizeof(interface.name));
    }
    return NS_OK;
  }
  nsTArray<NetworkInterface> mNetworkArray;
};
} // anonymous namespace

int
nr_stun_get_addrs(nr_transport_addr aAddrs[], int aMaxAddrs, int aDropLoopback, int *aCount)
{
  // Get network interface list.
  nsCOMPtr<GetNetworkInterfaceList> getNetworkInterfaceList = new GetNetworkInterfaceList();

  NS_DispatchToMainThread(getNetworkInterfaceList, NS_DISPATCH_SYNC);
  int32_t n = 0;
  for (uint32_t i = 0; i < getNetworkInterfaceList->mNetworkArray.Length(); i++) {
    if (aMaxAddrs <= n) {
      break;
    }
    NetworkInterface &interface = getNetworkInterfaceList->mNetworkArray.ElementAt(i);

    if (nr_sockaddr_to_transport_addr(reinterpret_cast<sockaddr*>(&(interface.addr)),
                                      sizeof(interface.addr),
                                      IPPROTO_UDP, 0, &(aAddrs[n]))) {
      NS_WARNING("Problem transforming address");
    } else {
      strlcpy(aAddrs[n].ifname, interface.name,
              sizeof(aAddrs[n].ifname));
      n++;
    }
  }

  *aCount = n;
  nr_stun_remove_duplicate_addrs(aAddrs, aDropLoopback, aCount);

  for (int i = 0; i < *aCount; ++i) {
    printf_stderr("Address %d: %s on %s", i,
                  aAddrs[i].as_string, aAddrs[i].ifname);
  }

  return 0;
}

#endif

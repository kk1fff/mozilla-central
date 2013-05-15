/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
extern "C" {
#include <arpa/inet.h>
#include "r_types.h"
#include "stun.h"
#include "addrs.h"
}

#include <vector>
#include <string>
#include "nsINetworkManager.h"
#include "nsINetworkInterfaceListService.h"
#include "runnable_utils.h"
#include "nsCOMPtr.h"
#include "nsThreadUtils.h"
#include "nsServiceManagerUtils.h"

namespace {
typedef struct {
  struct sockaddr_in addr;
  std::string name;
} NetworkInterface;

nsresult
GetInterfaces(std::vector<NetworkInterface>* aInterfaces)
{
  MOZ_ASSERT(aInterfaces);

  // Obtain network interfaces from network manager.
  nsresult rv;
  nsCOMPtr<nsINetworkInterfaceList> networkList;
  nsCOMPtr<nsINetworkInterfaceListService> listService =
    do_GetService("@mozilla.org/network/interface-list-service;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t flags =
    nsINetworkInterfaceListService::LIST_NOT_INCLUDE_SUPL_INTERFACES |
    nsINetworkInterfaceListService::LIST_NOT_INCLUDE_MMS_INTERFACES;
  NS_ENSURE_SUCCESS(listService->GetDataInterfaceList(flags,
                                                      getter_AddRefs(networkList)),
                    NS_ERROR_FAILURE);

  // Translate nsINetworkInterfaceList to NetworkInterface.
  int32_t listLength;
  NS_ENSURE_SUCCESS(networkList->GetNumberOfInterface(&listLength),
                    NS_ERROR_FAILURE);
  aInterfaces->clear();

  nsAutoString ip;
  nsAutoString ifaceName;
  for (int32_t i = 0; i < listLength; i++) {
    NetworkInterface interface;
    nsCOMPtr<nsINetworkInterface> iface;
    if (NS_FAILED(networkList->GetInterface(i, getter_AddRefs(iface)))) {
      continue;
    }

    memset(&(interface.addr), 0, sizeof(interface.addr));
    interface.addr.sin_family = AF_INET;
    iface->GetIp(ip);
    if (inet_pton(AF_INET, NS_ConvertUTF16toUTF8(ip).get(),
                  &(interface.addr.sin_addr.s_addr)) != 1) {
      continue;
    }

    iface->GetName(ifaceName);
    interface.name = NS_ConvertUTF16toUTF8(ifaceName).get();

    aInterfaces->push_back(interface);
  }
  return NS_OK;
}
} // anonymous namespace

int
nr_stun_get_addrs(nr_transport_addr aAddrs[], int aMaxAddrs,
                  int aDropLoopback, int* aCount)
{
  nsresult rv;
  int r;

  // Get network interface list.
  std::vector<NetworkInterface> interfaces;
  if (NS_FAILED(NS_DispatchToMainThread(
                    mozilla::WrapRunnableNMRet(&GetInterfaces, &interfaces, &rv),
                    NS_DISPATCH_SYNC)) || NS_FAILED(rv)) {
    return R_FAILED;
  }

  // Translate to nr_transport_addr.
  int32_t n = 0;
  for (std::vector<NetworkInterface>::iterator i = interfaces.begin();
       i != interfaces.end(); ++i) {
    if (aMaxAddrs <= n) {
      break;
    }
    if (nr_sockaddr_to_transport_addr(reinterpret_cast<sockaddr*>(&(i->addr)),
                                      sizeof(i->addr),
                                      IPPROTO_UDP, 0, &(aAddrs[n]))) {
      r_log(NR_LOG_STUN, LOG_WARNING, "Problem transforming address");
      return R_FAILED;
    }
    strlcpy(aAddrs[n].ifname, i->name.c_str(), sizeof(aAddrs[n].ifname));
    n++;
  }

  *aCount = n;
  r = nr_stun_remove_duplicate_addrs(aAddrs, aDropLoopback, aCount);
  if (r != 0) {
    return r;
  }

  for (int i = 0; i < *aCount; ++i) {
    printf_stderr("Address %d: %s on %s", i,
                  aAddrs[i].as_string, aAddrs[i].ifname);
  }

  return 0;
}

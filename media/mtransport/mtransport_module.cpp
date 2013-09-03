/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ModuleUtils.h"

#include "stun_udp_socket_filter.h"

NS_DEFINE_NAMED_CID(NS_STUN_UDP_SOCKET_FILTER_HANDLER_CID)

NS_GENERIC_FACTORY_CONSTRUCTOR(nsStunUDPSocketFilterHandler)

static const mozilla::Module::CIDEntry kMTransportCIDs[] = {
  { &kNS_STUN_UDP_SOCKET_FILTER_HANDLER_CID, false, NULL, nsStunUDPSocketFilterHandlerConstructor },
  {  NULL }
};

static const mozilla::Module::ContractIDEntry kMTransportContractIDs[] = {
  { NS_STUN_UDP_SOCKET_FILTER_HANDLER_CONTRACTID, &kNS_STUN_UDP_SOCKET_FILTER_HANDLER_CID },
  { NULL }
};

static const mozilla::Module kMTransportModule = {
    mozilla::Module::kVersion,
    kMTransportCIDs,
    kMTransportContractIDs,
    NULL,
    NULL,
    NULL,
    NULL
};

NSMODULE_DEFN(mtransport) = &kMTransportModule;

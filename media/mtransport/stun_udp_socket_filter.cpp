/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
extern "C" {
#include "stun.h"
#include "transport_addr.h"
}

#include <string>
#include <set>

#include "nsAutoPtr.h"
#include "mozilla/net/DNS.h"
#include "stun_udp_socket_filter.h"
#include "nr_socket_prsock.h"

namespace {

bool
GetAddressString(const mozilla::net::NetAddr *aAddr, std::string& result)
{
  nr_transport_addr addr;
  if (mozilla::nr_netaddr_to_transport_addr(aAddr, &addr)) {
    return false;
  }
  if (nr_transport_addr_fmt_addr_string(&addr)) {
    return false;
  }
  result = addr.as_string;
  return true;
}

class PendingSTUNRequest
{
public:
  const UINT12 id;
  const std::string remote;

  PendingSTUNRequest(const std::string &remoteAddress, const UINT12 &id)
    : id(id)
    , remote(remoteAddress) { }

  bool operator<(const PendingSTUNRequest& rhs) const
  {
    if (remote != rhs.remote) {
      return remote < rhs.remote;
    }

    for (int i = 0; i < 12; ++i) {
      if (id.octet[i] != rhs.id.octet[i]) {
        return id.octet[i] < rhs.id.octet[i];
      }
    }

    return false;
  }
};

class nsStunUDPSocketFilter : public nsIUDPSocketFilter
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIUDPSOCKETFILTER

  nsStunUDPSocketFilter();

private:
  ~nsStunUDPSocketFilter();

  bool FilterIncomingPacket(const mozilla::net::NetAddr *aRemoteAddr,
                            const uint8_t *aData,
                            uint32_t aLen);

  bool FilterOutgoingPacket(const mozilla::net::NetAddr *aRemoteAddr,
                            const uint8_t *aData,
                            uint32_t aLen);

  std::set<std::string> mWhiteList;
  std::set<PendingSTUNRequest> mPendingRequests;
};

NS_IMPL_ISUPPORTS1(nsStunUDPSocketFilter, nsIUDPSocketFilter)

nsStunUDPSocketFilter::nsStunUDPSocketFilter() { }

nsStunUDPSocketFilter::~nsStunUDPSocketFilter() { }

NS_IMETHODIMP
nsStunUDPSocketFilter::SetLocalAddress(const mozilla::net::NetAddr *aLocalAddr,
                                       bool *aResult)
{
  *aResult = true;
  return NS_OK;
}

NS_IMETHODIMP
nsStunUDPSocketFilter::FilterPacket(const mozilla::net::NetAddr *aRemoteAddr,
                                    const uint8_t *aData,
                                    uint32_t aLen,
                                    int32_t aDirection,
                                    bool *aResult)
{
  switch (aDirection) {
  case nsIUDPSocketFilter::SF_INCOMING:
    *aResult = FilterIncomingPacket(aRemoteAddr, aData, aLen);
    break;
  case nsIUDPSocketFilter::SF_OUTGOING:
    *aResult = FilterOutgoingPacket(aRemoteAddr, aData, aLen);
    break;
  default:
    MOZ_CRASH("Unknown packet direction");
  }
  return NS_OK;
}

bool
nsStunUDPSocketFilter::FilterIncomingPacket(const mozilla::net::NetAddr *aRemoteAddr,
                                            const uint8_t *aData, uint32_t aLen) {
  std::string remoteAddress;
  NS_ENSURE_TRUE(GetAddressString(aRemoteAddr, remoteAddress), false);

  // Check white list
  if (mWhiteList.find(remoteAddress) != mWhiteList.end()) {
    return true;
  }

  // Check if this is a stun message.
  if (nr_is_stun_message(reinterpret_cast<UCHAR*>(const_cast<uint8_t*>(aData)), aLen) > 0) {
    // Check if we had sent any stun request to this destination. If we had sent a request
    // to this host, we check the transaction id, and we can add this address to whitelist.
    std::set<PendingSTUNRequest>::iterator i =
      mPendingRequests.find(PendingSTUNRequest(remoteAddress,
                                               ((nr_stun_message_header*)aData)->id));
    if (i != mPendingRequests.end()) {
      mPendingRequests.erase(i);
      mWhiteList.insert(remoteAddress);
      return true;
    }
  }

  return false;
}

bool
nsStunUDPSocketFilter::FilterOutgoingPacket(const mozilla::net::NetAddr *aRemoteAddr,
                                            const uint8_t *aData,
                                            uint32_t aLen) {
  std::string remoteAddress;
  NS_ENSURE_TRUE(GetAddressString(aRemoteAddr, remoteAddress), false);

  // Check white list
  if (mWhiteList.find(remoteAddress) != mWhiteList.end()) {
    return true;
  }

  // Check if it is a stun packet. If yes, we put it into a pending list and wait for
  // response packet.
  if (nr_is_stun_message(reinterpret_cast<UCHAR*>(const_cast<uint8_t*>(aData)), aLen) == 3) {
    mPendingRequests.insert(PendingSTUNRequest(remoteAddress, ((nr_stun_message_header*)aData)->id));
    return true;
  }

  return false;
}

} // anonymous namespace

NS_IMPL_ISUPPORTS1(nsStunUDPSocketFilterHandler, nsIUDPSocketFilterHandler)

nsStunUDPSocketFilterHandler::nsStunUDPSocketFilterHandler() { }

nsStunUDPSocketFilterHandler::~nsStunUDPSocketFilterHandler() { }

NS_IMETHODIMP nsStunUDPSocketFilterHandler::NewFilter(nsIUDPSocketFilter **aResult)
{
  nsIUDPSocketFilter *ret = new nsStunUDPSocketFilter();
  if (!ret) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  nsCOMPtr<nsIUDPSocketFilter>(ret).forget(aResult);
  return NS_OK;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <string>
#include <set>

extern "C" {
#include "stun.h"
}

#include "mozilla/net/DNS.h"
#include "stun_udp_socket_filter.h"
#include "nr_socket_prsock.h"

namespace {

bool operator<(const mozilla::net::NetAddr &lhs,
               const mozilla::net::NetAddr &rhs) {
  // For IPv4 case, which we support, the fields we care about use fewer space
  // then whole structure, so we compare address and port. For other case, we
  // simply do memcmp, if the structures (including unused bytes) are not exactly
  // the same, it would result in mismatch.
  if (lhs.raw.family != rhs.raw.family) {
    return lhs.raw.family < rhs.raw.family;
  }
  if (lhs.raw.family == AF_INET) {
    return lhs.inet.ip < rhs.inet.ip ? true : (lhs.inet.port < rhs.inet.port);
  }

  return memcmp(&lhs, &rhs, sizeof(mozilla::net::NetAddr)) < 0;
}

bool operator!=(const mozilla::net::NetAddr &lhs,
                const mozilla::net::NetAddr &rhs) {
  return lhs < rhs || rhs < lhs;
}

class CompareNetAddr {
 public:
  bool operator() (const mozilla::net::NetAddr &lhs,
                   const mozilla::net::NetAddr &rhs) {
    return lhs < rhs;
  }
};

class PendingSTUNRequest {
 public:
  PendingSTUNRequest(const mozilla::net::NetAddr &netaddr, const UINT12 &id)
    : id_(id),
      net_addr_(netaddr) {}

  bool operator<(const PendingSTUNRequest& rhs) const {
    if (net_addr_ != rhs.net_addr_) {
      return net_addr_ < rhs.net_addr_;
    }
    return memcmp(id_.octet, rhs.id_.octet, sizeof(id_.octet)) < 0;
  }

 private:
  const UINT12 id_;
  const mozilla::net::NetAddr net_addr_;
};

class STUNUDPSocketFilter : public nsIUDPSocketFilter {
 public:
  STUNUDPSocketFilter()
    : white_list_(),
      pending_requests_() {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIUDPSOCKETFILTER

 private:
  bool filter_incoming_packet(const mozilla::net::NetAddr *remote_addr,
                              const uint8_t *data,
                              uint32_t len);

  bool filter_outgoing_packet(const mozilla::net::NetAddr *remote_addr,
                              const uint8_t *data,
                              uint32_t len);

  std::set<mozilla::net::NetAddr, CompareNetAddr> white_list_;
  std::set<PendingSTUNRequest> pending_requests_;
};

NS_IMPL_ISUPPORTS1(STUNUDPSocketFilter, nsIUDPSocketFilter)

NS_IMETHODIMP
STUNUDPSocketFilter::FilterPacket(const mozilla::net::NetAddr *remote_addr,
                                  const uint8_t *data,
                                  uint32_t len,
                                  int32_t direction,
                                  bool *result) {
  switch (direction) {
    case nsIUDPSocketFilter::SF_INCOMING:
      *result = filter_incoming_packet(remote_addr, data, len);
      break;
    case nsIUDPSocketFilter::SF_OUTGOING:
      *result = filter_outgoing_packet(remote_addr, data, len);
      break;
    default:
      MOZ_CRASH("Unknown packet direction");
  }
  return NS_OK;
}

bool STUNUDPSocketFilter::filter_incoming_packet(const mozilla::net::NetAddr *remote_addr,
                                                 const uint8_t *data, uint32_t len) {
  // Check white list
  if (white_list_.find(*remote_addr) != white_list_.end()) {
    return true;
  }

  // Check if this is a stun message.
  if (nr_is_stun_response_message(reinterpret_cast<UCHAR*>(const_cast<uint8_t*>(data)), len)) {
    const nr_stun_message_header *msg = reinterpret_cast<const nr_stun_message_header*>(data);

    // Check if we had sent any stun request to this destination. If we had sent a request
    // to this host, we check the transaction id, and we can add this address to whitelist.
    std::set<PendingSTUNRequest>::iterator it =
      pending_requests_.find(PendingSTUNRequest(*remote_addr, msg->id));
    if (it != pending_requests_.end()) {
      pending_requests_.erase(it);
      white_list_.insert(*remote_addr);
      return true;
    }
  }

  return false;
}

bool STUNUDPSocketFilter::filter_outgoing_packet(const mozilla::net::NetAddr *remote_addr,
                                                 const uint8_t *data,
                                                 uint32_t len) {
  // Check white list
  if (white_list_.find(*remote_addr) != white_list_.end()) {
    return true;
  }

  // Check if it is a stun packet. If yes, we put it into a pending list and wait for
  // response packet.
  if (nr_is_stun_request_message(reinterpret_cast<UCHAR*>(const_cast<uint8_t*>(data)), len)) {
    const nr_stun_message_header *msg = reinterpret_cast<const nr_stun_message_header*>(data);

    pending_requests_.insert(PendingSTUNRequest(*remote_addr, msg->id));
    return true;
  }

  return false;
}

} // anonymous namespace

NS_IMPL_ISUPPORTS1(nsStunUDPSocketFilterHandler, nsIUDPSocketFilterHandler)

NS_IMETHODIMP nsStunUDPSocketFilterHandler::NewFilter(nsIUDPSocketFilter **result)
{
  nsIUDPSocketFilter *ret = new STUNUDPSocketFilter();
  if (!ret) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  NS_ADDREF(*result = ret);
  return NS_OK;
}

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
extern "C" {
#include "stun.h"
}

#include "nsAutoPtr.h"
#include "stun_udp_socket_filter.h"

namespace {

class nsStunUDPSocketFilter : public nsIUDPSocketFilter
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIUDPSOCKETFILTER

  nsStunUDPSocketFilter();

private:
  ~nsStunUDPSocketFilter();

protected:
};

NS_IMPL_ISUPPORTS1(nsStunUDPSocketFilter, nsIUDPSocketFilter)

nsStunUDPSocketFilter::nsStunUDPSocketFilter()
{
  printf_stderr("nsStunUDPSocketFilter: created\n");
}

nsStunUDPSocketFilter::~nsStunUDPSocketFilter()
{

}

NS_IMETHODIMP
nsStunUDPSocketFilter::SetLocalAddress(mozilla::net::NetAddr *aLocalAddr,
                                       bool *aResult)
{
  *aResult = true;
  return NS_OK;
}

NS_IMETHODIMP
nsStunUDPSocketFilter::FilterPacket(mozilla::net::NetAddr *aRemoteAddr,
                                    const uint8_t *aData,
                                    uint32_t aLen,
                                    int32_t aDirection,
                                    bool *aResult)
{
  printf_stderr("nsStunUDPSocketFilter: Filter, direction = %s\n",
                aDirection == nsIUDPSocketFilter::SF_INCOMING ? "INCOMING" :
                aDirection == nsIUDPSocketFilter::SF_OUTGOING ? "OUTGOING" : "UNKNOWN?");
  *aResult = true;
  return NS_OK;
}

} // anonymous namespace

NS_IMPL_ISUPPORTS1(nsStunUDPSocketFilterHandler, nsIUDPSocketFilterHandler)

nsStunUDPSocketFilterHandler::nsStunUDPSocketFilterHandler()
{
  printf_stderr("nsStunUDPSocketFilterHandler: created\n");
}

nsStunUDPSocketFilterHandler::~nsStunUDPSocketFilterHandler()
{

}

NS_IMETHODIMP nsStunUDPSocketFilterHandler::NewFilter(nsIUDPSocketFilter **aResult)
{
  nsIUDPSocketFilter *ret = new nsStunUDPSocketFilter();
  if (!ret) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  nsCOMPtr<nsIUDPSocketFilter>(ret).forget(aResult);
  return NS_OK;
}

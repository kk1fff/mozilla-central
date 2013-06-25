#include <algorithm>
#include <vector>
#include <string>
#include "nr_api.h"
#include "nrinterfacepriority.h"

#include <stdio.h>

namespace {

class LocalAddress {
public:
  explicit LocalAddress(nr_local_addr* aLocalAddr)
    : mIsVpn((aLocalAddr->interface.type & NR_INTERFACE_TYPE_VPN) != 0)
    , mEstimatedSpeed(aLocalAddr->interface.estimated_speed)
    , mTypePreferance(GetNetworkTypePreference(aLocalAddr->interface.type)) {
    char buf[50];
    nr_transport_addr_fmt_ifname_addr_string(&aLocalAddr->addr, buf, 50);
    mKey = buf;
  }

  bool operator<(const LocalAddress& rhs) const {
    // If type preferance is different, we should simply sort by mTypePreferance.
    if (mTypePreferance != rhs.mTypePreferance) {
      return mTypePreferance < rhs.mTypePreferance;
    }

    // If type preferance is the same, the next thing we use to sort is vpn.
    // If two LocalAddress are different in mIsVpn, the LocalAddress that is
    // not in vpn gets priority.
    if (mIsVpn != rhs.mIsVpn) {
      return !mIsVpn; // If |this| instance is not in VPN, |rhs| must in VPN,
                      // we return true to make |this| sorted prior to |rhs|.
    }

    // Compare estimated speed.
    if (mEstimatedSpeed != rhs.mEstimatedSpeed) {
      return mEstimatedSpeed > rhs.mEstimatedSpeed;
    }

    // All things above are the same, we can at least sort with key.
    return mKey < rhs.mKey;
  }

  const std::string& GetKey() const {
    return mKey;
  }

private:
  // Getting the preferance corresponding to a type. Getting lower number here
  // means the type of network is preferred.
  static inline int GetNetworkTypePreference(int type) {
    if (type & NR_INTERFACE_TYPE_WIRED) {
      return 1;
    }
    if (type & NR_INTERFACE_TYPE_WIFI) {
      return 2;
    }
    if (type & NR_INTERFACE_TYPE_MOBILE) {
      return 3;
    }

    return 4;
  }

  std::string mKey;
  bool mIsVpn;
  int mEstimatedSpeed;
  int mTypePreferance;
};

typedef std::vector<LocalAddress> LocalAddrList;

class InterfacePrioritizer {
public:
  int add(nr_local_addr *aIface) {
    char buf[50];
    nr_transport_addr_fmt_ifname_addr_string(&aIface->addr, buf, 50);
    for (LocalAddrList::const_iterator i = mLocalAddrs.begin();
         i != mLocalAddrs.end(); i++) {
      if (i->GetKey() == buf) {
        // Local address with the same key is already in mLocalAddrs.
        return 0;
      }
    }
    mLocalAddrs.push_back(LocalAddress(aIface));
    return 0;
  }

  int sort() {
    std::sort(mLocalAddrs.begin(), mLocalAddrs.end());
    return 0;
  }

  int getPreference(const char *key, int *pref) {
    int tmpPref = 127;
    for (LocalAddrList::const_iterator i = mLocalAddrs.begin();
         i != mLocalAddrs.end(); i++) {
      if (i->GetKey() == key) {
        // Local address with the same key is already in mLocalAddrs.
        *pref = tmpPref;
        return 0;
      }
      tmpPref--;
    }
    return R_NOT_FOUND;
  }

private:
  LocalAddrList mLocalAddrs;
};

} // anonymous namespace

static int add_interface(void *obj, nr_local_addr *iface) {
  InterfacePrioritizer *ip = static_cast<InterfacePrioritizer*>(obj);
  return ip->add(iface);
}

static int get_priority(void *obj, char *key, int *pref) {
  InterfacePrioritizer *ip = static_cast<InterfacePrioritizer*>(obj);
  return ip->getPreference(key, pref);
}

static int sort_preference(void *obj) {
  InterfacePrioritizer *ip = static_cast<InterfacePrioritizer*>(obj);
  return ip->sort();
}

static int destroy(void **obj) {
  if (!obj || !*obj) {
    return 0;
  }

  InterfacePrioritizer *ip = static_cast<InterfacePrioritizer*>(*obj);
  *obj = 0;
  delete ip;

  return 0;
}

static nr_interface_priority_vtbl nr_socket_local_vtbl = {
  add_interface,
  get_priority,
  sort_preference,
  destroy
};

namespace mozilla {

nr_interface_priority* CreateIntefacePriority() {
  nr_interface_priority *ip;
  int r = nr_interface_priority_create_int(new InterfacePrioritizer(),
                                           &nr_socket_local_vtbl,
                                           &ip);
  if (r != 0) {
    return nullptr;
  }
  return ip;
}

} // namespace mozilla

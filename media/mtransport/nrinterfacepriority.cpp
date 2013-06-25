/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include "nr_api.h"
#include "nrinterfacepriority.h"
#include "nsCOMPtr.h"

#ifndef USE_INTERFACE_PRIORITY_FALLBACK

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

#else // USE_INTERFACE_PRIORITY_FALLBACK

namespace {

typedef std::map<std::string, int> PreferenceMap;

static int add_interface(void *obj, nr_local_addr *iface) {
  return 0;
}

static int get_priority(void *obj, char *key, int *pref) {
  // Using hardcorded priority decision as fallback.
  static PreferenceMap preferenceMap;
  if (preferenceMap.size() == 0) {
    preferenceMap["rl0"] = 255;
    preferenceMap["wi0"] = 254;
    preferenceMap["lo0"] = 253;
    preferenceMap["en1"] = 252;
    preferenceMap["en0"] = 251;
    preferenceMap["eth0"] = 252;
    preferenceMap["eth1"] = 251;
    preferenceMap["eth2"] = 249;
    preferenceMap["ppp"] = 250;
    preferenceMap["ppp0"] = 249;
    preferenceMap["en2"] = 248;
    preferenceMap["en3"] = 247;
    preferenceMap["em0"] = 251;
    preferenceMap["em1"] = 252;
    preferenceMap["vmnet0"] = 240;
    preferenceMap["vmnet1"] = 241;
    preferenceMap["vmnet3"] = 239;
    preferenceMap["vmnet4"] = 238;
    preferenceMap["vmnet5"] = 237;
    preferenceMap["vmnet6"] = 236;
    preferenceMap["vmnet7"] = 235;
    preferenceMap["vmnet8"] = 234;
    preferenceMap["virbr0"] = 233;
    preferenceMap["wlan0"] = 232;
  }

  int i = 0;
  while (key[i] != ':' && key[i] != '\0') {
    i++;
  }
  PreferenceMap::iterator preference = preferenceMap.find(std::string(key, i));
  if (preference != preferenceMap.end()) {
    *pref = preference->second;
  } else {
    *pref = 0;
  }
  return 0;
}

static int sort_preference(void *obj) {
  return 0;
}

static int destroy(void **obj) {
  return 0;
}

static nr_interface_priority_vtbl nr_socket_local_vtbl = {
  add_interface,
  get_priority,
  sort_preference,
  destroy
};

} // anonymous namespace

namespace mozilla {

nr_interface_priority* CreateIntefacePriority() {
  nr_interface_priority *ip;
  int r = nr_interface_priority_create_int(nullptr, &nr_socket_local_vtbl, &ip);
  if (r != 0) {
    return nullptr;
  }
  return ip;
}

} // namespace mozilla

#endif // USE_INTERFACE_PRIORITY_FALLBACK


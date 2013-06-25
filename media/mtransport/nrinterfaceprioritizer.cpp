/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include "logging.h"
#include "nrinterfaceprioritizer.h"
#include "nsCOMPtr.h"

MOZ_MTLOG_MODULE("mtransport")

namespace {

class LocalAddress {
public:
  explicit LocalAddress(const nr_local_addr& local_addr)
    : is_vpn_(false),
      estimated_speed_(-1),
      type_preference_(-1)
  {
    char buf[50];
    nr_transport_addr_fmt_ifname_addr_string(&local_addr.addr, buf, sizeof(buf));
    key_ = buf;
    is_vpn_ = (local_addr.interface.type & NR_INTERFACE_TYPE_VPN) != 0;
    estimated_speed_ = local_addr.interface.estimated_speed;
    type_preference_ = GetNetworkTypePreference(local_addr.interface.type);
  }

  bool operator<(const LocalAddress& rhs) const {
    // If type preferences are different, we should simply sort by
    // |type_preference_|.
    if (type_preference_ != rhs.type_preference_) {
      return type_preference_ < rhs.type_preference_;
    }

    // If type preferences are the same, the next thing we use to sort is vpn.
    // If two LocalAddress are different in |is_vpn_|, the LocalAddress that is
    // not in vpn gets priority.
    if (is_vpn_ != rhs.is_vpn_) {
      return !is_vpn_; // If |this| instance is not in VPN, |rhs| must in VPN,
                       // we return true to make |this| sorted prior to |rhs|.
    }

    // Compare estimated speed.
    if (estimated_speed_ != rhs.estimated_speed_) {
      return estimated_speed_ > rhs.estimated_speed_;
    }

    // All things above are the same, we can at least sort with key.
    return key_ < rhs.key_;
  }

  const std::string& GetKey() const {
    return key_;
  }
private:
  // Getting the preference corresponding to a type. Getting lower number here
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

  std::string key_;
  bool is_vpn_;
  int estimated_speed_;
  int type_preference_;
};

class InterfacePrioritizer {
public:
  InterfacePrioritizer(): sorted_(false) {}

  int add(nr_local_addr *iface) {
    std::pair<std::set<LocalAddress>::iterator, bool> r =
      local_addrs_.insert(LocalAddress(*iface));
    if (!r.second) {
      return R_ALREADY; // This address is already in the set.
    }
    sorted_ = false;
    return 0;
  }

  int sort() {
    UCHAR tmpPref = 127;
    preference_map_.clear();
    for (std::set<LocalAddress>::iterator i = local_addrs_.begin();
         i != local_addrs_.end(); ++i) {
      preference_map_.insert(make_pair(i->GetKey(), tmpPref--));
    }
    sorted_ = true;
    return 0;
  }

  int getPreference(const char *key, UCHAR *pref) {
    if (!sorted_) {
      return R_FAILED;
    }
    std::map<std::string, UCHAR>::iterator i = preference_map_.find(key);
    if (i == preference_map_.end()) {
      return R_NOT_FOUND;
    }
    *pref = i->second;
    return 0;
  }

private:
  std::set<LocalAddress> local_addrs_;
  std::map<std::string, UCHAR> preference_map_;
  bool sorted_;
};

} // anonymous namespace

static int add_interface(void *obj, nr_local_addr *iface) {
  InterfacePrioritizer *ip = static_cast<InterfacePrioritizer*>(obj);
  return ip->add(iface);
}

static int get_priority(void *obj, const char *key, UCHAR *pref) {
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

static nr_interface_prioritizer_vtbl priorizer_vtbl = {
  add_interface,
  get_priority,
  sort_preference,
  destroy
};

namespace mozilla {

nr_interface_prioritizer* CreateIntefacePrioritizer() {
  nr_interface_prioritizer *ip;
  int r = nr_interface_prioritizer_create_int(new InterfacePrioritizer(),
                                              &priorizer_vtbl,
                                              &ip);
  if (r != 0) {
    return nullptr;
  }
  return ip;
}

} // namespace mozilla

namespace {

typedef std::map<std::string, UCHAR> PreferenceMap;

static int fallback_add_interface(void *obj, nr_local_addr *iface) {
  return 0;
}

static int fallback_get_priority(void *obj, const char *key, UCHAR *pref) {
  // Using hardcorded priority decision as fallback.
  static PreferenceMap preferenceMap;
  static UCHAR next_automatic_preference = 224;
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
  std::string ifname(key, i);
  PreferenceMap::iterator preference = preferenceMap.find(ifname);
  if (preference != preferenceMap.end()) {
    *pref = preference->second;
  } else {
    // Try to assign a preference.
    if (next_automatic_preference == 1) {
      MOZ_MTLOG(PR_LOG_DEBUG, "Out of preference values. Can't assign "
                "one for interface " << ifname);
      return R_NOT_FOUND;
    }
    MOZ_MTLOG(PR_LOG_DEBUG, "Automatically assigning preference for "
              "interface " << ifname << "->" <<
              next_automatic_preference);
    preferenceMap[ifname] = *pref = next_automatic_preference--;
  }
  return 0;
}

static int fallback_sort_preference(void *obj) {
  return 0;
}

static int fallback_destroy(void **obj) {
  return 0;
}

static nr_interface_prioritizer_vtbl fallback_priorizer_vtbl = {
  fallback_add_interface,
  fallback_get_priority,
  fallback_sort_preference,
  fallback_destroy
};

} // anonymous namespace

namespace mozilla {

nr_interface_prioritizer* CreateFallbackIntefacePrioritizer() {
  nr_interface_prioritizer *ip;
  int r = nr_interface_prioritizer_create_int(nullptr, &fallback_priorizer_vtbl, &ip);
  if (r != 0) {
    return nullptr;
  }
  return ip;
}

} // namespace mozilla


#include <algorithm>
#include <vector>
#include <string>
#include "nr_api.h"
#include "nrinterfacepriority.h"

namespace {

typedef std::vector<nr_local_addr> LocalAddrList;

class InterfacePrioritizer {
public:
  int add(nr_local_addr *aIface) {
    mLocalAddrs.push_back(*aIface);
    return 0;
  }

  int sort() {
    std::sort(mLocalAddrs.begin(), mLocalAddrs.end(),
              &InterfacePrioritizer::sortingFunc);
    return 0;
  }

  int getPreference(char *key, int *pref) {
    int tmpPref = 127;
    for (LocalAddrList::iterator i = mLocalAddrs.begin();
         i != mLocalAddrs.end(); ++i) {
      if (strcmp(i->addr.as_string, key) == 0) {
        *pref = tmpPref;
        return 0;
      }
      tmpPref--;
    }
    return R_NOT_FOUND;
  }

private:
  /**
   * Assign a value for a network type. The rank is used to decide the
   * preference of network types.
   * WIRED < WIFI < MOBILE < UNKNOWN
   */
  static inline int getTypeRank(int type) {
    if (type & NR_INTERFACE_TYPE_WIRED) {
      return 1;
    }
    if (type & NR_INTERFACE_TYPE_WIFI) {
      return 2;
    }
    if (type & NR_INTERFACE_TYPE_MOBILE) {
      return 3;
    }

    // We don't care whether NR_INTERFACE_TYPE_VPN is set here.
    // For interface that is not mobile, wifi or wired, we treat it as unknown.
    return 4;
  }

  static bool sortingFunc(const nr_local_addr &addr1, const nr_local_addr &addr2) {
    // When types are different (exclude vpn), we decide priority by types.
    // Turn on vpn for comparsion.
    if (getTypeRank(addr1.interface.type) != getTypeRank(addr2.interface.type)) {
      return getTypeRank(addr1.interface.type) < getTypeRank(addr2.interface.type);
    }

    // When types are the same, we compare VPN.
    if (addr1.interface.type & NR_INTERFACE_TYPE_VPN !=
        addr2.interface.type & NR_INTERFACE_TYPE_VPN) {
      // In this case, one of ns_local_addrs must be VPN and the other is not.
      // The ns_local_addrs that is not VPN should be put before the other one.

      // Return true if addr1 is the interface that is not VPN.
      return (addr1.interface.type & NR_INTERFACE_TYPE_VPN == 0);
    }

    // Comparing speed.
    if (addr1.interface.estimated_speed != addr2.interface.estimated_speed) {
      return addr1.interface.estimated_speed > addr2.interface.estimated_speed;
    }

    // If all of the properties are the same, compare as_string.
    return strcmp(addr1.addr.as_string, addr2.addr.as_string);
  }

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

#include <algorithm>
#include <vector>
#include <string>
#include "nr_api.h"
#include "nrinterfacepriority.h"

#include <stdio.h>
#include <iostream>
#include <fstream>

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

  //private:
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
    fprintf(stderr, "Patrick: key string: \"%s\"\n", buf);
    for (LocalAddrList::const_iterator i = mLocalAddrs.begin();
         i != mLocalAddrs.end(); i++) {
      if (i->GetKey() == buf) {
        // Local address with the same key is already in mLocalAddrs.
        return 0;
      }
    }
    mLocalAddrs.push_back(LocalAddress(aIface));
    fprintf(stderr, "Patrick: adding interface: %s\n", aIface->addr.as_string);
    return 0;
  }

  int sort() {
    std::sort(mLocalAddrs.begin(), mLocalAddrs.end());
    return 0;
  }

  int getPreference(const char *key, int *pref) {
    fprintf(stderr, "Patrick: getting preference of interface, key: %s, current length of mLocalAddrs: %d\n", key, mLocalAddrs.size());
    int tmpPref = 127;
    for (LocalAddrList::const_iterator i = mLocalAddrs.begin();
         i != mLocalAddrs.end(); i++) {
      fprintf(stderr, "Patrick: testing: \"%s\"\n", i->GetKey().c_str());
      if (i->GetKey() == key) {
        // Local address with the same key is already in mLocalAddrs.
        fprintf(stderr, "Patrick: Priority of [%s], type=%d, estimated_speed=%d, is vpn = %d, pref=%d\n",
                key, i->mTypePreferance, i->mEstimatedSpeed, i->mIsVpn, tmpPref);
        *pref = tmpPref;
        return 0;
      }
      tmpPref--;
    }
    return R_NOT_FOUND;
  }

  void dump() {
    for (LocalAddrList::const_iterator i = mLocalAddrs.begin(); i != mLocalAddrs.end(); i++) {
      fprintf(stderr, "Patrick: dumping: \"%s\" type=%d, estimated_speed=%d, is vpn = %d\n",
              i->GetKey().c_str(), i->mTypePreferance, i->mEstimatedSpeed, i->mIsVpn);
    }
  }

private:
  LocalAddrList mLocalAddrs;
};


void init_local_addr(nr_local_addr *la, const char *ip, short port, const char *ifname,
                     int estimatedSpeed, bool isVpn, int type) {
  la->interface.type = 0;
  if (isVpn) {
    la->interface.type |= NR_INTERFACE_TYPE_VPN;
  }
  switch (type) {
  case 1: // ethernet
    la->interface.type |= NR_INTERFACE_TYPE_WIRED;
    break;
  case 2: // wifi
    la->interface.type |= NR_INTERFACE_TYPE_WIFI;
    break;
  case 3: // mobile
    la->interface.type |= NR_INTERFACE_TYPE_MOBILE;
    break;
  default:
    break;
  }

  la->interface.estimated_speed = estimatedSpeed;

  struct sockaddr_in addr;
  inet_aton(ip, &addr.sin_addr);
  addr.sin_port = 0;
  addr.sin_family = AF_INET;
  nr_sockaddr_to_transport_addr((sockaddr*)&addr, sizeof(sockaddr_in), IPPROTO_UDP, 0, &la->addr);
}

std::string get_key(nr_local_addr *la) {
  char buf[100];
  nr_transport_addr_fmt_ifname_addr_string(&la->addr, buf, 100);
  return buf;
}

int get_pref(InterfacePrioritizer &ip, nr_local_addr *la) {
  int pref;
  ip.getPreference(get_key(la).c_str(), &pref);
  return pref;
}

int TestLocalAddr() {
  InterfacePrioritizer ip;
  std::ifstream f("/home/patrick/t/iftest");
  while (!f.eof()) {
    nr_local_addr la;
    std::string ipaddr, iname;
    int port, vpn, speed, type;
    f >> ipaddr >> port >> iname >> speed >> vpn >> type;
    init_local_addr(&la, ipaddr.c_str(), port, iname.c_str(), speed, vpn != 0, type);
    ip.add(&la);
  }
  ip.sort();
  ip.dump();
  return 0;
};

static int test = TestLocalAddr();

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

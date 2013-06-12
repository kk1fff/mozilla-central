#include "local_addr.h"
#include "transport_addr.h"

int nr_local_addr_copy(nr_transport_addr *to, nr_transport_addr *from)
  {
    nr_transport_addr_copy(&(to->addr), &(from->addr));
    memcpy(&(to->interface), &(from->interface), sizeof(nr_local_addr));
    return(0);
  }

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <nr_api.h>
#include "local_addr.h"

int nr_local_addr_copy(nr_local_addr *to, nr_local_addr *from)
  {
    nr_transport_addr_copy(&(to->addr), &(from->addr));
    memcpy(&(to->interface), &(from->interface), sizeof(nr_local_addr));
    return(0);
  }

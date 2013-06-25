/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "nr_api.h"
#include "nr_interface_priority.h"
#include "transport_addr.h"

int nr_interface_priority_create_int(void *obj,nr_interface_priority_vtbl *vtbl,
  nr_interface_priority **ifpp)
  {
    int _status;
    nr_interface_priority *ifp=0;

    if(!(ifp=RCALLOC(sizeof(nr_interface_priority))))
      ABORT(R_NO_MEMORY);

    ifp->obj = obj;
    ifp->vtbl = vtbl;

    *ifpp = ifp;

    _status=0;
  abort:
    return(_status);
  }

int nr_interface_priority_destroy(nr_interface_priority **ifpp)
  {
    nr_interface_priority *ifp;
    if (!ifpp || !*ifpp)
      return(0);

    ifp = *ifpp;
    *ifpp = 0;

    if (ifp->vtbl)
      ifp->vtbl->destroy(&ifp->obj);

    RFREE(ifp);

    return(0);
  }

int nr_interface_priority_add_interface(nr_interface_priority *ifp,
  nr_local_addr *addr)
  {
    return ifp->vtbl->add_interface(ifp->obj, addr);
  }

int nr_interface_priority_get_proirity(nr_interface_priority *ifp,
  char *as_string, int *interface_preference)
  {
    return ifp->vtbl->get_priority(ifp->obj,as_string,interface_preference);
  }

int nr_interface_priority_sort_preference(nr_interface_priority *ifp)
  {
    return ifp->vtbl->sort_preference(ifp->obj);
  }

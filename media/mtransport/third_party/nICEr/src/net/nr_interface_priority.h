/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _nr_interface_priorirty
#define _nr_interface_priorirty

#include "transport_addr.h"
#include "local_addr.h"

typedef struct nr_interface_priority_vtbl_ {
  int (*add_interface)(void *obj, nr_local_addr *iface);
  int (*get_priority)(void *obj, char *as_string, int *pref);
  int (*sort_preference)(void *obj);
  int (*destroy)(void **obj);
} nr_interface_priority_vtbl;

typedef struct nr_interface_priority_ {
  void *obj;
  nr_interface_priority_vtbl *vtbl;
} nr_interface_priority;

int nr_interface_priority_create_int(void *obj, nr_interface_priority_vtbl *vtbl,
                                     nr_interface_priority **ifacepriorityp);

int nr_interface_priority_destroy(nr_interface_priority **ifacepriorityp);

int nr_interface_priority_add_interface(nr_interface_priority *ifacepriority,
                                        nr_local_addr *addr);

int nr_interface_priority_get_proirity(nr_interface_priority *ifacepriority,
                                       char *ifacename, int *interface_preference);

int nr_interface_priority_sort_preference(nr_interface_priority *ifacepriority);
#endif

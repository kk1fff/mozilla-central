/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://gre/modules/Services.jsm");

const NETWORKLISTSERVICE_CONTRACTID = "@mozilla.org/network/interface-list-manager;1";
const NETWORKLISTSERVICE_CID = Components.ID("{bf6dcfc5-f628-4923-ba43-7159691129e7}");

XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsISyncMessageSender");

function NetworkInterfaceListService () {
  cpmm.addEventListener('NetworkInterfaceList:ListInterface:OK', this);
  cpmm.addEventListener('NetworkInterfaceList:ListInterface:KO', this);
}

NetworkInterfaceListService.prototype = {
  _id: 0,
  _requests: {},
  getInterfacesList: function(aCallback) {
    let currentId = this._id++;
    cpmm.sendAsyncMessage('NetworkInterfaceList:ListInterface', {
      id: currentId
    });
    this._requests[currentId] = {
      callback: aCallback
    };
  },

  receiveMessage: function(aMsg) {
    let reqId = aMsg.json.id,
        req = this._requests[reqId];

    if (!req) {
      return;
    }

    let callback = req.callback,
        param = null;
    switch(aMsg.name) {
    case 'NetworkInterfaceList:ListInterface:OK':
      param = new NetworkInterfaceList(aMsg.json.interfaces);
      break;
    case 'NetworkInterfaceList:ListInterface:KO':
      // Leave param null
    }
    try {
      callback.QueryInterface(Ci.nsINetworkInterfaceListCallback).onInterfaceListGot(param);
    } catch(e) {
      dump("Error in callback of NetworkInterfaceList: " + e);
      // Throw this error away.
    }
  }
};

function NetworkInterfaceList (aInterfaces) {
  this._interfaces = aInterfaces;
}

NetworkInterfaceList.prototype = {
  getNumberOfInterface: function() {
    return this._interfaces.length;
  },

  getInterface: function(index) {
    if (!this._interfaces) return null;
    return this._interfaces[index];
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([NetworkInterfaceListService]);


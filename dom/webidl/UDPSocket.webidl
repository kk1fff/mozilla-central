/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://raw-sockets.sysapps.org/#idl-def-UDPSocket
 */

[Constructor (optional UDPOptions options)]
interface UDPSocket : EventTarget {
  readonly    attribute DOMString       localAddress;
  readonly    attribute unsigned short  localPort;
  readonly    attribute DOMString?      remoteAddress;
  readonly    attribute unsigned short? remotePort;
  readonly    attribute boolean         addressReuse;
  readonly    attribute boolean         loopback;
  readonly    attribute unsigned long   bufferedAmount;
  readonly    attribute ReadyState      readyState;
  attribute EventHandler    onopen;
  attribute EventHandler    ondrain;
  attribute EventHandler    onerror;
  attribute EventHandler    onmessage;
  void    close ();
  void    suspend ();
  void    resume ();
  void    joinMulticast (DOMString multicastGroupAddress);
  void    leaveMulticast (DOMString multicastGroupAddress);
  boolean send ((DOMString or Blob or ArrayBuffer or ArrayBufferView) data, optional DOMString? remoteAddress, optional unsigned short? remotePort);
};

const {classes: Cc, interfaces: Ci, utils: Cu, results: Cr} = Components;
Cu.import('resource://gre/modules/XPCOMUtils.jsm');
Cu.import('resource://gre/modules/Services.jsm');

const prefixString = "Patrick's test la!";

function log(msg) {
  dump("PatrickProtocol: " + msg + "\n");
}

function PatrickChannel(aURI) {
  this.originalURI = aURI;
  this.URI = aURI;
}

PatrickChannel.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIChannel]),
  owner: null,
  notificationCallbacks: null,
  securityInfo: null,
  contentType: "text/plain",
  contentLength: -1, // unknown
  open: function() {
    throw Cr.NS_ERROR_NOT_IMPLEMENTED;
  },
  asyncOpen: function(listener, ctx) {
    let channel = this;
    log("asyncOpen!!");
    Services.tm.currentThread.dispatch({
      run: function() {
        let stream = Cc["@mozilla.org/io/string-input-stream;1"].
              createInstance(Ci.nsIStringInputStream);
        let data = prefixString;
        stream.setData(data, data.length);
        listener.onStartRequest(channel, ctx);
        listener.onDataAvailable(channel, ctx, stream, 0, data.length);
        listener.onStopRequest(channel, ctx, Cr.NS_OK);
      }
    }, Ci.nsIThread.DISPATCH_NORMAL);
  }

};

function PatrickProtocolHandler() {}

PatrickProtocolHandler.prototype = {
  classID: Components.ID("{a9d0645d-3029-4547-aea5-2207e60defdb}"),
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIProtocolHandler]),
  scheme: "patrick",
  defaultPort: -1,
  protocolFlags: Ci.nsIProtocolHandler.URI_NORELATIVE |
    Ci.nsIProtocolHandler.URI_NOAUTH |
    Ci.nsIProtocolHandler.URI_LOADABLE_BY_ANYONE |
    Ci.nsIProtocolHandler.URI_DOES_NOT_RETURN_DATA,
  
  newURI: function(aSpec, aOriginCharset, aBaseURI) {
    log("newURI!! " + aSpec);
    let uri = Cc["@mozilla.org/network/simple-uri;1"].createInstance(Ci.nsIURI);
    uri.spec = aSpec;
    return uri;
  },

  newChannel: function(aURI) {
    log("newChannel!!");
    return new PatrickChannel(aURI);
  },

  allowPort: function(aPort, aScheme) {
    return true;
  }
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([PatrickProtocolHandler]);

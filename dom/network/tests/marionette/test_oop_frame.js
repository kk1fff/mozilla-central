/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

MARIONETTE_TIMEOUT = 60000;

const browserElementTestHelpers = {
  _getBoolPref: function(pref) {
    try {
      return SpecialPowers.getBoolPref(pref);
    }
    catch (e) {
      return undefined;
    }
  },

  _setPref: function(pref, value) {
    this.lockTestReady();
    if (value !== undefined && value !== null) {
      SpecialPowers.pushPrefEnv({'set': [[pref, value]]}, this.unlockTestReady.bind(this));
    } else {
      SpecialPowers.pushPrefEnv({'clear': [[pref]]}, this.unlockTestReady.bind(this));
    }
  },

  _testReadyLockCount: 0,
  _firedTestReady: false,
  lockTestReady: function() {
    this._testReadyLockCount++;
    console.log("Patrick lock: " + this._testReadyLockCount);
  },

  unlockTestReady: function() {
    this._testReadyLockCount--;
    console.log("Patrick unlock: " + this._testReadyLockCount);
    if (this._testReadyLockCount == 0 && !this._firedTestReady) {
      this._firedTestReady = true;
      dispatchEvent(new Event("testready"));
    }
  },

  enableProcessPriorityManager: function() {
    this._setPref('dom.ipc.processPriorityManager.testMode', true);
    this._setPref('dom.ipc.processPriorityManager.enabled', true);
  },

  setEnabledPref: function(value) {
    this._setPref('dom.mozBrowserFramesEnabled', value);
  },

  getOOPByDefaultPref: function() {
    return this._getBoolPref("dom.ipc.browser_frames.oop_by_default");
  },

  addPermission: function() {
    SpecialPowers.addPermission("browser", true, document);
    this.tempPermissions.push(location.href);
  },

  removeAllTempPermissions: function() {
    for(var i = 0; i < this.tempPermissions.length; i++) {
      SpecialPowers.removePermission("browser", this.tempPermissions[i]);
    }
  },

  addPermissionForUrl: function(url) {
    SpecialPowers.addPermission("browser", true, url);
    this.tempPermissions.push(url);
  },

  'tempPermissions': []
};

// Set some prefs:
//
//  * browser.pageThumbs.enabled: false
//
//    Disable tab view; it seriously messes us up.
//
//  * dom.ipc.browser_frames.oop_by_default
//
//    Enable or disable OOP-by-default depending on the test's filename.  You
//    can still force OOP on or off with <iframe mozbrowser remote=true/false>,
//    at least until bug 756376 lands.
//
//  * dom.ipc.tabs.disabled: false
//
//    Allow us to create OOP frames.  Even if they're not the default, some
//    "in-process" tests create OOP frames.
//
//  * network.disable.ipc.security: true
//
//    Disable the networking security checks; our test harness just tests
//    browser elements without sticking them in apps, and the security checks
//    dislike that.
//
//    Unfortunately setting network.disable.ipc.security to false before the
//    child process(es) created by this test have shut down can cause us to
//    assert and kill the child process.  That doesn't cause the tests to fail,
//    but it's still scary looking.  So we just set the pref to true and never
//    pop that value.  We'll rely on the tests which test IPC security to set
//    it to false.
//
//  * security.mixed_content.block_active_content: false
//
//    Disable mixed active content blocking, so that tests can confirm that mixed
//    content results in a broken security state.

(function() {
  var oop = true;
  let loader = SpecialPowers.Cc["@mozilla.org/moz/jssubscript-loader;1"]
        .getService(SpecialPowers.Ci.mozIJSSubScriptLoader);
  let specialpowers = {};
  SpecialPowers.wrap(loader)
    .loadSubScript("chrome://specialpowers/content/SpecialPowersObserver.js",
                   specialpowers);
  specialpowers.specialPowersObserver = new specialpowers.SpecialPowersObserver();
  specialpowers.specialPowersObserver.init();

  browserElementTestHelpers.lockTestReady();
  SpecialPowers.setBoolPref("network.disable.ipc.security", true);
  SpecialPowers.pushPrefEnv({set: [["browser.pageThumbs.enabled", false],
                                   ["dom.ipc.browser_frames.oop_by_default", oop],
                                   ["dom.ipc.tabs.disabled", false],
                                   ["security.mixed_content.block_active_content", false]]},
                            browserElementTestHelpers.unlockTestReady.bind(browserElementTestHelpers));
})();

addEventListener('unload', function() {
  browserElementTestHelpers.removeAllTempPermissions();
});

browserElementTestHelpers.setEnabledPref(true);
browserElementTestHelpers.addPermission();

function contentScript() {
  function ok(val, msg) {
    if (val) {
      sendAsyncMessage("test-ok", { ok: true, msg: msg });
    } else {
      sendAsyncMessage("test-ok", { ok: false, msg: msg });
    }
  }

  function is(val1, val2, msg) {
    if (val1 == val2) {
      sendAsyncMessage("test-ok", { ok: true, msg: msg });
    } else {
      sendAsyncMessage("test-ok", { ok: false, msg: "" + val1 + " = " + val2 + msg });
    }
  }

  function finish() {
    sendAsyncMessage("test-fisish");
  }

  function attachToWindow(aEvent) {
    var window = aEvent.target.defaultView;
    try {
      if ((window !== null) &&
          (window !== undefined) &&
          (window.wrappedJSObject) &&
          (window.parent !== null) &&
          (window.parent !== undefined) &&
          window.location.hostname == window.parent.location.hostname) {
        window.wrappedJSObject.is = is;
        window.wrappedJSObject.ok = ok;
        window.wrappedJSObject.finish = finish;
      }
    } catch(ex) {
      sendAsyncMessage("error", { msg: "Cannot attach is/ok to window: " + ex });
    }
  }
  addEventListener("DOMWindowCreated", attachToWindow, false);
  dump("Patrick: youou");
}

function runTest() {
  var iframe = document.createElement('iframe');
  SpecialPowers.wrap(iframe).mozbrowser = true;
  document.body.appendChild(iframe);
  let mm = SpecialPowers.getBrowserFrameMessageManager(iframe);
  let specialPowersBase = "chrome://specialpowers/content/";
  mm.loadFrameScript(specialPowersBase + "MozillaLogger.js", false);
  mm.loadFrameScript(specialPowersBase + "specialpowersAPI.js", false);
  mm.loadFrameScript(specialPowersBase + "specialpowers.js", false);
  mm.loadFrameScript("data:text/javascript,(" + contentScript.toString() + ")()", false);

  mm.addMessageListener('test-ok', function(msg) {
    msg = SpecialPowers.wrap(msg);
    ok(msg.json.ok, msg.json.msg);
  });

  mm.addMessageListener('error', function(msg) {
    msg = SpecialPowers.wrap(msg);
    ok(false, msg.json.msg);
    finish();
  });

  mm.addMessageListener('test-finish', function(msg) {
    finish();
  });

  iframe.addEventListener("mozbrowserclose", function(e) {
    ok(true, "got mozbrowserclose event.");
    SimpleTest.finish();
  });
//  iframe.src = "data:text/html,<html><body><script>ok(true, 'test la');</scr" + "ipt></html>";
  iframe.src = "data:text/html,<html><body><script>(" + testInFrame.toString() + ")()</scr" + "ipt></body></html>";
}

addEventListener('testready', runTest);

function testInFrame() {
  const OPERATOR_HOME = 0;
  const OPERATOR_ROAMING = 1;
 
  SpecialPowers.addPermission('mobileconnection', true, document);
  var connection = navigator.mozMobileConnection;
  ok(connection instanceof MozMobileConnection,
     'connection is instanceof ' + connection.constructor);
}
//   let voice = connection.voice;
//   ok(voice, "voice connection valid");
// 
//   let network = voice.network;
//   ok(network, "voice network info valid");
// 
//   let emulatorCmdPendingCount = 0;
//   function sendEmulatorCommand(cmd, callback) {
//     emulatorCmdPendingCount++;
//     runEmulatorCmd(cmd, function (result) {
//       emulatorCmdPendingCount--;
// 
//       is(result[result.length - 1], "OK");
// 
//       callback(result);
//     });
//   }
// 
//   function setEmulatorOperatorNames(which, longName, shortName, callback) {
//     let cmd = "operator set " + which + " " + longName + "," + shortName;
//     sendEmulatorCommand(cmd, function (result) {
//       let re = new RegExp("^" + longName + "," + shortName + ",");
//       ok(result[which].match(re), "Long/short name should be changed.");
// 
//       if (callback) {
//         window.setTimeout(callback, 0);
//       }
//     });
//   }
// 
//   function setEmulatorRoaming(roaming, callback) {
//     let cmd = "gsm voice " + (roaming ? "roaming" : "home");
//     sendEmulatorCommand(cmd, function (result) {
//       is(result[0], "OK");
// 
//       if (callback) {
//         window.setTimeout(callback, 0);
//       }
//     });
//   }
// 
//   function checkValidMccMnc() {
//     is(network.mcc, "310", "network.mcc");
//     is(network.mnc, "260", "network.mnc");
//   }
// 
//   function waitForVoiceChange(callback) {
//     connection.addEventListener("voicechange", function onvoicechange() {
//       connection.removeEventListener("voicechange", onvoicechange);
//       callback();
//     });
//   }
// 
//   function doTestMobileOperatorNames(longName, shortName, callback) {
//     log("Testing '" + longName + "', '" + shortName + "':");
// 
//     checkValidMccMnc();
// 
//     waitForVoiceChange(function () {
//       is(network.longName, longName, "network.longName");
//       is(network.shortName, shortName, "network.shortName");
// 
//       checkValidMccMnc();
// 
//       window.setTimeout(callback, 0);
//     });
// 
//     setEmulatorOperatorNames(OPERATOR_HOME, longName, shortName);
//   }
// 
//   function testMobileOperatorNames() {
//     doTestMobileOperatorNames("Mozilla", "B2G", function () {
//       doTestMobileOperatorNames("Mozilla", "", function () {
//         doTestMobileOperatorNames("", "B2G", function () {
//           doTestMobileOperatorNames("", "", function () {
//             doTestMobileOperatorNames("Android", "Android", testRoamingCheck);
//           });
//         });
//       });
//     });
//   }
// 
//   // See bug 797972 - B2G RIL: False roaming situation
//   //
//   // Steps to test:
//   // 1. set roaming operator names
//   // 2. set emulator roaming
//   // 3. wait for onvoicechange event and test passing conditions
//   // 4. set emulator roaming back to false
//   // 5. wait for onvoicechange event again and callback
//   function doTestRoamingCheck(longName, shortName, callback) {
//     log("Testing roaming check '" + longName + "', '" + shortName + "':");
// 
//     setEmulatorOperatorNames(OPERATOR_ROAMING, longName, shortName,
//                              window.setTimeout.bind(window, function () {
//                                let done = false;
//                                function resetRoaming() {
//                                  if (!done) {
//                                    window.setTimeout(resetRoaming, 100);
//                                    return;
//                                  }
// 
//                                  waitForVoiceChange(callback);
//                                  setEmulatorRoaming(false);
//                                }
// 
//                                waitForVoiceChange(function () {
//                                  is(network.longName, longName, "network.longName");
//                                  is(network.shortName, shortName, "network.shortName");
//                                  is(voice.roaming, false, "voice.roaming");
// 
//                                  resetRoaming();
//                                });
// 
//                                setEmulatorRoaming(true, function () {
//                                  done = true;
//                                });
//                              }, 3000) // window.setTimeout.bind
//                             ); // setEmulatorOperatorNames
//   }
// 
//   function testRoamingCheck() {
//     // If Either long name or short name of current registered operator matches
//     // SPN("Android"), then the `roaming` attribute should be set to false.
//     doTestRoamingCheck("Android", "Android", function () {
//       doTestRoamingCheck("Android", "android", function () {
//         doTestRoamingCheck("Android", "Xxx", function () {
//           doTestRoamingCheck("android", "Android", function () {
//             doTestRoamingCheck("android", "android", function () {
//               doTestRoamingCheck("android", "Xxx", function () {
//                 doTestRoamingCheck("Xxx", "Android", function () {
//                   doTestRoamingCheck("Xxx", "android", function () {
//                     setEmulatorOperatorNames(OPERATOR_ROAMING, "TelKila", "TelKila",
//                                              window.setTimeout.bind(window, cleanUp, 3000));
//                   });
//                 });
//               });
//             });
//           });
//         });
//       });
//     });
//   }
// 
//   function cleanUp() {
//     if (emulatorCmdPendingCount > 0) {
//       setTimeout(cleanUp, 100);
//       return;
//     }
// 
//     SpecialPowers.removePermission("mobileconnection", document);
//     finish();
//   }
// 
//   waitFor(testMobileOperatorNames, function () {
//     return voice.connected;
//   });
// }



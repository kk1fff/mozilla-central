/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const { classes: Cc, interfaces: Ci, utils: Cu } = Components;
this.EXPORTED_SYMBOLS = [ ];

Cu.import("resource://gre/modules/devtools/gcli.jsm");
Cu.import('resource://gre/modules/XPCOMUtils.jsm');

XPCOMUtils.defineLazyModuleGetter(this, "gDevTools",
                                  "resource:///modules/devtools/gDevTools.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "devtools",
                                  "resource:///modules/devtools/gDevTools.jsm");

/**
 * 'inspect' command
 */
gcli.addCommand({
  name: "inspect",
  description: gcli.lookup("inspectDesc"),
  manual: gcli.lookup("inspectManual"),
  params: [
    {
      name: "selector",
      type: "node",
      description: gcli.lookup("inspectNodeDesc"),
      manual: gcli.lookup("inspectNodeManual")
    }
  ],
  exec: function Command_inspect(args, context) {
    let gBrowser = context.environment.chromeDocument.defaultView.gBrowser;
    let target = devtools.TargetFactory.forTab(gBrowser.selectedTab);

    return gDevTools.showToolbox(target, "inspector").then(function(toolbox) {
      toolbox.getCurrentPanel().selection.setNode(args.selector, "gcli");
    }.bind(this));
  }
});

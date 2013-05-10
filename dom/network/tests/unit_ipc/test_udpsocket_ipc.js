Components.utils.import("resource://gre/modules/Services.jsm");

function run_test() {
  run_test_in_child("/udpsocket_child.js", function() {
    do_test_finished();
  });
}

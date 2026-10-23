# gui-test: sync
# expect: FAIL: gui_dialog accept: no modal dialog within 200 ms
gui_start
gui_dialog accept -timeout 200

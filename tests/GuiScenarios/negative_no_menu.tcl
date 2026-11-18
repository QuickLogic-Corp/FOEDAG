# gui-test: sync
# expect: FAIL: gui_menu Run: no menu open within 200 ms
gui_start
gui_menu Run -timeout 200

# gui-test: sync
# expect: FAIL: gui_wait_for {0}: timed out after 200 ms: never true
gui_start
gui_wait_for {0} -timeout 200 -message "never true"

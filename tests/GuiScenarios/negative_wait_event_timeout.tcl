# gui-test: sync
# expect: FAIL: gui_wait_event task.done: timed out after 200 ms; last milestones:
gui_start
gui_trigger_action actionAbout
gui_dialog accept
gui_wait_event task.done -timeout 200

# gui-test: sync
# expect: FAIL: gui_assert {[gui_get actionExit enabled] == 0}: false: the action is enabled
gui_start
gui_assert {[gui_get actionExit enabled] == 0} "the action is enabled"

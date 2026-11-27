# gui-test: sync
# expect: FAIL: gui_screenshot negative_missing_golden/console: missing golden
gui_start
gui_wait_for {[gui_find TclConsole] ne ""}
gui_screenshot negative_missing_golden/console -widget TclConsole

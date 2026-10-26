# gui-test: sync
# needs: goldens
# expect: FAIL: gui_screenshot negative_screenshot_mismatch/console: mismatch: size differs
# Its golden is a deliberately wrong 2x2 image, so the gate must fail.
gui_start
gui_wait_for {[gui_find TclConsole] ne ""}
gui_screenshot negative_screenshot_mismatch/console -widget TclConsole

# gui-test: sync
# A modal dialog answered by the script: File > Exit, then No, leaves the main
# window open. The line runner keeps running lines inside the dialog's loop.
gui_start
gui_trigger_action actionExit
gui_wait_event dialog.shown
gui_dialog reject
gui_wait_event dialog.closed
gui_assert {[gui_get actionExit enabled]} "the main window is still open"
gui_screenshot exit_cancel/01_main_window

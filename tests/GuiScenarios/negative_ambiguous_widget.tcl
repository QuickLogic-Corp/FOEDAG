# gui-test: sync
# expect: FAIL: gui_find qt_dockwidget_closebutton: 
# expect: visible widgets named 'qt_dockwidget_closebutton'
gui_start
gui_wait_for {[gui_find TclConsole] ne ""}
gui_find qt_dockwidget_closebutton

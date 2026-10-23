# gui-test: sync
# G-005 step 1 (device-free): a command typed in the Tcl console prints its output.
gui_start
gui_wait_for {[gui_find TclConsole] ne ""} -message "the console is shown"
gui_set_text TclConsole {puts hello}
gui_wait_for {[regexp -line {^hello$} [gui_get TclConsole -text]]} -message "console shows the line 'hello'"

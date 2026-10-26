# gui-test: sync
# G-014 (device-free): checkpoints taken while recording replay as passing
# asserts. Without --record, gui_test_checkpoint only checks the widget exists.
gui_start
gui_wait_for {[gui_find TclConsole] ne ""} -message "the console is shown"
gui_set_text TclConsole {puts hello}
gui_wait_for {[regexp -line {^hello$} [gui_get TclConsole -text]]} -message "console shows the line 'hello'"
gui_trigger_action actionNewProject
gui_wait_event dialog.shown newProjectDialog
gui_set_text m_lineEditPname gui_checkpoint
gui_set_text m_lineEditPpath $::gui_test::project_dir
gui_test_checkpoint m_lineEditPname
gui_click newProjectNext
gui_test_checkpoint m_radioBtnRTL
gui_click newProjectCancel
gui_wait_event dialog.closed newProjectDialog

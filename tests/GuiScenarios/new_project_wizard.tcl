# gui-test: sync
# G-004 (device-free part): the New Project wizard up to the device page.
gui_start
gui_trigger_action actionNewProject
gui_wait_event dialog.shown newProjectDialog
gui_assert {[gui_get newProjectTabs -text] eq "Project Directory"}
gui_set_text m_lineEditPname gui_new_project
gui_set_text m_lineEditPpath [pwd]
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Type of Project"}
gui_assert {[gui_get m_radioBtnRTL checked]}
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Add Design Files"}
gui_set_text lineEditTopModule counter
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Target Device"}
gui_click newProjectBack
gui_assert {[gui_get lineEditTopModule -text] eq "counter"} "the wizard keeps page values when going back"
gui_click newProjectCancel
gui_wait_event dialog.closed newProjectDialog
gui_assert {![file exists gui_new_project]} "a cancelled wizard creates no project"

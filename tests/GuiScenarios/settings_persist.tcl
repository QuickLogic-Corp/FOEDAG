# gui-test: sync
# G-005 steps 2-3 (device-free): a task setting and a project setting changed in
# Project settings persist after the dialog is reopened, and in the project files.
gui_start
gui_trigger_action actionNewProject
gui_wait_event dialog.shown newProjectDialog
gui_set_text m_lineEditPname gui_settings
gui_set_text m_lineEditPpath [pwd]
gui_click newProjectNext
gui_click newProjectNext
gui_set_text lineEditTopModule counter
gui_click newProjectNext
gui_click newProjectNext
gui_click newProjectNext
gui_click newProjectFinish
gui_wait_event dialog.closed newProjectDialog
gui_wait_for {[gui_get actionProjectSettings enabled]} -message "a project is open"

gui_trigger_action actionProjectSettings
gui_wait_event dialog.shown newProjectDialog
gui_select newProjectTabs "Task Settings"
gui_assert {![gui_get verific checked]}
gui_click verific
gui_click taskSettingsApply
gui_dialog accept
gui_wait_for {![gui_get taskSettingsApply enabled]} -message "Apply saves the task settings"
gui_select newProjectTabs "Design Files"
gui_set_text lineEditTopModule counter_top
gui_click newProjectFinish
gui_wait_event dialog.closed newProjectDialog

gui_trigger_action actionProjectSettings
gui_wait_event dialog.shown newProjectDialog
gui_select newProjectTabs "Task Settings"
gui_assert {[gui_get verific checked]} "the task setting persists"
gui_select newProjectTabs "Design Files"
gui_assert {[gui_get lineEditTopModule -text] eq "counter_top"} "the project setting persists"
gui_click newProjectCancel
gui_wait_event dialog.closed newProjectDialog

proc read_file {path} { set f [open $path]; set data [read $f]; close $f; return $data }
gui_assert {[string first {"userValue": "checked"} [read_file gui_settings/gui_settings.json]] >= 0} "verific is saved to the project settings file"
gui_assert {[string first counter_top [read_file gui_settings/gui_settings.ospr]] >= 0} "the top module is saved to the project file"

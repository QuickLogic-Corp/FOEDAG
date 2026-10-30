# gui-test: sync
# G-004 (device-free part): the New Project wizard up to the device page.
gui_start
gui_trigger_action actionNewProject
gui_wait_event dialog.shown newProjectDialog
gui_assert {[gui_get newProjectTabs -text] eq "Project Directory"}
gui_set_text m_lineEditPname gui_new_project
# A relative location reads the same on every machine; the default one is the
# home directory, so the page is captured only after this.
gui_set_text m_lineEditPpath .
gui_screenshot new_project_wizard/01_directory -widget newProjectDialog
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Type of Project"}
gui_assert {[gui_get m_radioBtnRTL checked]}
gui_screenshot new_project_wizard/02_type -widget newProjectDialog
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Add Design Files"}
gui_set_text lineEditTopModule counter_16bit
# The Location column shows the file's directory, so it must be the same on
# every run; the run directory is not.
set design_dir /tmp/gui_new_project_wizard
file mkdir $design_dir
set f [open $design_dir/counter_16bit.v w]
puts $f "module counter_16bit (input clk, output reg \[15:0\] count);\nendmodule"
close $f
gui_click sourceGridAddFile
gui_wait_event dialog.shown
gui_set_text fileNameEdit /tmp/gui_new_project_wizard/counter_16bit.v
gui_dialog accept
gui_wait_event dialog.closed
gui_screenshot new_project_wizard/03_design_files -widget newProjectDialog
gui_click newProjectNext
gui_assert {[gui_get newProjectTabs -text] eq "Target Device"}
gui_screenshot new_project_wizard/04_device -widget newProjectDialog
gui_click newProjectBack
gui_assert {[gui_get lineEditTopModule -text] eq "counter_16bit"} "the wizard keeps page values when going back"
gui_click newProjectCancel
gui_wait_event dialog.closed newProjectDialog
gui_assert {![file exists gui_new_project]} "a cancelled wizard creates no project"

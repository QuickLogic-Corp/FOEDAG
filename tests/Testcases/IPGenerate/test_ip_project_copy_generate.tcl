# First of two sessions (the second is test_ip_project_copy_reuse.tcl):
# generate an IP from the catalog, so that the project holds its own copy.

set platform $::tcl_platform(platform)
if { $platform == "windows" } {
    puts "SKIPPING ON WINDOWS: This test requires python which FileUtils::ExecuteSystemCommand() currently fails to find on Windows.\n"
    exit 0
}

proc fail { msg } {
    puts "TEST FAILED: $msg"
    exit 1
}

file delete -force ip_project_copy

create_design ip_project_copy
architecture ../../Arch/k6_frac_N10_tileable_40nm.xml ../../Arch/k6_N10_40nm_openfpga.xml
add_litex_ip_catalog ./IP_Catalog

configure_ip axis_converter_V1_0 -mod_name kept -version V1_0 -Pcore_in_width=128 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0
ipgenerate

set copy ip_project_copy/run_1/IPs/RapidSilicon/IP/axis_converter/V1_0/kept
if { ![file exists $copy/axis_converter_V1_0_kept.json] } {
    fail "ipgenerate left no cache file in $copy"
}

# A real generator writes the wrapper into src/. The dummy generators of this
# catalog do not all do that, so make sure the copy has one source file.
if { [llength [glob -nocomplain -directory $copy/src *]] == 0 } {
    file mkdir $copy/src
    set fp [open $copy/src/kept.v w]
    puts $fp "module kept();\nendmodule"
    close $fp
}

set fp [open "aurora.log" r]
set log [read $fp]
close $fp
if { [string first "is not in any IP catalog" $log] >= 0 } {
    fail "an IP that is in the catalog must not be taken from the project"
}

puts "TEST PASSED"
exit 0

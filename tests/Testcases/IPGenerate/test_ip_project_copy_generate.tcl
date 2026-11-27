# First of two sessions (the second is test_ip_project_copy_reuse.tcl):
# generate an IP from the catalog, so that the project holds its own copy, and
# check that the copy is then used as it is until the request changes.

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

proc read_log {} {
    set fp [open "aurora.log" r]
    set log [read $fp]
    close $fp
    return $log
}

# Once generated, the IP is used from the project: with the same parameters
# ipgenerate runs no generator, whatever else the cache file records. The
# build directory and the netlists_fingerprint of earlier versions used to
# take part in the comparison.
set cache $copy/axis_converter_V1_0_kept.json
set fp [open $cache r]
set text [read $fp]
close $fp
regsub {"build_dir": "[^"]*"} $text {"build_dir": "/moved/elsewhere"} text
regsub {"json_template"} $text "\"netlists_fingerprint\": \"a.eblif:1:2;\",\n   \"json_template\"" text
set fp [open $cache w]
puts -nonewline $fp $text
close $fp
ipgenerate
set log [read_log]
if { [regexp -all {IP Generate, generating IP} $log] != 1 } {
    fail "ipgenerate ran the generator again for unchanged parameters"
}
if { [string first "IP Generate, reusing IP" $log] < 0 } {
    fail "ipgenerate did not say it reused the copy"
}

# Other parameters are a new request and are generated from the catalog. There
# is one copy per module: it then records the second request, so the first
# one is generated again as well.
configure_ip axis_converter_V1_0 -mod_name kept -version V1_0 -Pcore_in_width=64 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0
ipgenerate
if { [regexp -all {IP Generate, generating IP} [read_log]] != 2 } {
    fail "ipgenerate did not generate the IP for other parameters"
}
configure_ip axis_converter_V1_0 -mod_name kept -version V1_0 -Pcore_in_width=128 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0
ipgenerate
if { [regexp -all {IP Generate, generating IP} [read_log]] != 3 } {
    fail "ipgenerate did not generate the IP for the first parameters again"
}

# To take the catalog's current IP when the parameters have not changed,
# delete the instance and configure it again: delete_ip removes the copy, so
# ipgenerate has to run the generator.
delete_ip kept
if { [file exists $copy] } {
    fail "delete_ip left the copy in $copy"
}
configure_ip axis_converter_V1_0 -mod_name kept -version V1_0 -Pcore_in_width=128 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0
ipgenerate
if { [regexp -all {IP Generate, generating IP} [read_log]] != 4 } {
    fail "ipgenerate did not generate the IP again after delete_ip"
}
if { ![file exists $cache] } {
    fail "ipgenerate left no cache file in $copy"
}

# A real generator writes the wrapper into src/. The dummy generators of this
# catalog do not all do that, so make sure the copy has one source file for
# test_ip_project_copy_reuse.tcl.
if { [llength [glob -nocomplain -directory $copy/src *]] == 0 } {
    file mkdir $copy/src
    set fp [open $copy/src/kept.v w]
    puts $fp "module kept();\nendmodule"
    close $fp
}

set log [read_log]
if { [string first "is not in any IP catalog" $log] >= 0 } {
    fail "an IP that is in the catalog must not be taken from the project"
}

puts "TEST PASSED"
exit 0

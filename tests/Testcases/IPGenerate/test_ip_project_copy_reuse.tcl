# Second of two sessions (the first is test_ip_project_copy_generate.tcl).
# The catalog that carries axis_converter_V1_0 is not loaded here, which is
# what a project sees after an update dropped the IP. The project still holds
# the copy the first session generated and must keep building from it.

set platform $::tcl_platform(platform)
if { $platform == "windows" } {
    puts "SKIPPING ON WINDOWS: This test requires python which FileUtils::ExecuteSystemCommand() currently fails to find on Windows.\n"
    exit 0
}

proc fail { msg } {
    puts "TEST FAILED: $msg"
    exit 1
}

proc readFile { name } {
    set fp [open $name r]
    set data [read $fp]
    close $fp
    return $data
}

set copy ip_project_copy/run_1/IPs/RapidSilicon/IP/axis_converter/V1_0/kept
set cache $copy/axis_converter_V1_0_kept.json
if { ![file exists $cache] } {
    fail "no generated copy in $copy: run test_ip_project_copy_generate.tcl first"
}
set recorded [readFile $cache]

set ip axis_converter_V1_0
set params {-Pcore_in_width=128 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0}

# create_design on the existing project keeps run_1/IPs.
create_design ip_project_copy
architecture ../../Arch/k6_frac_N10_tileable_40nm.xml ../../Arch/k6_N10_40nm_openfpga.xml

# Other parameters cannot be served from the copy, and say why.
if { ![catch {configure_ip $ip -mod_name kept -version V1_0 -Pcore_in_width=64 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0} msg] } {
    fail "configure_ip must refuse parameters the copy was not generated with"
}
if { [string first "Unknown IP: $ip" $msg] != 0 || [string first "other parameters" $msg] < 0 } {
    fail "expected 'Unknown IP: $ip' and the reason, got: $msg"
}

# A module the project never generated is simply an unknown IP.
if { ![catch {configure_ip $ip -mod_name never_generated -version V1_0 {*}$params} msg] } {
    fail "configure_ip must fail for a module with no generated copy"
}
if { $msg ne "Unknown IP: $ip" } {
    fail "expected exactly 'Unknown IP: $ip', got: $msg"
}

# The parameters of the copy: accepted, with a warning.
if { [catch {configure_ip $ip -mod_name kept -version V1_0 {*}$params} msg] } {
    fail "configure_ip must fall back to the generated copy: $msg"
}
set log [readFile aurora.log]
if { [string first "IP '$ip' is not in any IP catalog. Using the copy this project generated earlier in" $log] < 0 } {
    fail "configure_ip did not warn that the project copy is in use"
}
if { [string first "IPs/RapidSilicon/IP/axis_converter/V1_0/kept; it cannot be reconfigured or regenerated." $log] < 0 } {
    fail "the warning does not name the copy's directory"
}

# ipgenerate runs nothing and leaves the copy alone.
ipgenerate
set log [readFile aurora.log]
if { ![regexp {IP Generate, reusing IP \S*IPs/RapidSilicon/IP/axis_converter/V1_0/kept \(project copy: 'axis_converter_V1_0' is not in any IP catalog\)} $log] } {
    fail "ipgenerate did not report that it reuses the project copy"
}
if { [readFile $cache] ne $recorded } {
    fail "ipgenerate must not rewrite the cache file of a project copy"
}

# Not sticky: once the catalog carries the IP again, it is used.
add_litex_ip_catalog ./IP_Catalog
set warnings [regexp -all {Using the copy this project generated earlier} [readFile aurora.log]]
if { [catch {configure_ip $ip -mod_name kept -version V1_0 {*}$params} msg] } {
    fail "configure_ip from the catalog failed: $msg"
}
if { [regexp -all {Using the copy this project generated earlier} [readFile aurora.log]] != $warnings } {
    fail "an IP that is back in the catalog must not be taken from the project"
}

puts "TEST PASSED"
exit 0

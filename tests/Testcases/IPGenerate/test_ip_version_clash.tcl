# One module name used by two versions of an IP (axis_converter V1_0, V1_1).
# configure_ip must reject the second real instance instead of letting
# ipgenerate silently rebuild the first.

set platform $::tcl_platform(platform)
if { $platform == "windows" } {
    puts "SKIPPING ON WINDOWS: This test requires python which FileUtils::ExecuteSystemCommand() currently fails to find on Windows.\n"
    exit 0
}

proc fail { msg } {
    puts "TEST FAILED: $msg"
    exit 1
}

set params {-Pcore_in_width=128 -Pcore_out_width=64 -Pcore_user_width=0 -Pcore_reverse=0}

create_design ip_version_clash
architecture ../../Arch/k6_frac_N10_tileable_40nm.xml ../../Arch/k6_N10_40nm_openfpga.xml
add_litex_ip_catalog ./IP_Catalog

configure_ip axis_converter_V1_0 -mod_name shared -version V1_0 {*}$params -out_location rs_ips

# A GUI preview of the other version may reuse the name; it is not an error.
if { [catch {configure_ip axis_converter_V1_1 -mod_name shared -version V1_1 {*}$params -out_location rs_ips -template} msg] } {
    fail "a -template preview must not clash with a real instance: $msg"
}

if { ![catch {configure_ip axis_converter_V1_1 -mod_name shared -version V1_1 {*}$params -out_location rs_ips} msg] } {
    fail "configure_ip must reject a second version under module name 'shared'"
}
if { [string first "Module name 'shared' is already used by axis_converter_V1_0 (V1_0)" $msg] < 0 } {
    fail "the clash error must name the instance that holds the name, got: $msg"
}

# Same version is a reconfigure, whatever the case of -version.
if { [catch {configure_ip axis_converter_V1_0 -mod_name shared -version v1_0 {*}$params -out_location rs_ips} msg] } {
    fail "reconfiguring the same version must succeed: $msg"
}

# A distinct name for the other version works.
configure_ip axis_converter_V1_1 -mod_name shared_v1_1 -version V1_1 {*}$params -out_location rs_ips

ipgenerate -modules {shared shared_v1_1}

set fp [open "aurora.log" r]
set log [read $fp]
close $fp
if { ![regexp {V1_0/shared(?!_)} $log] } {
    fail "ipgenerate -modules shared must generate the V1_0 instance"
}
if { [regexp {V1_1/shared(?!_)} $log] } {
    fail "the rejected V1_1 instance must not be generated under 'shared'"
}
if { ![regexp {V1_1/shared_v1_1} $log] } {
    fail "ipgenerate -modules shared_v1_1 must generate the V1_1 instance"
}

puts "TEST PASSED"

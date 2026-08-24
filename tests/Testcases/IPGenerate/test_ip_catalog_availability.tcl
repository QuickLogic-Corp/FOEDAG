#Copyright 2022 The Foedag team

#GPL License

#Copyright (c) 2022 The Open-Source FPGA Foundation

#This program is free software: you can redistribute it and/or modify
#it under the terms of the GNU General Public License as published by
#the Free Software Foundation, either version 3 of the License, or
#(at your option) any later version.

#This program is distributed in the hope that it will be useful,
#but WITHOUT ANY WARRANTY; without even the implied warranty of
#MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#GNU General Public License for more details.

#You should have received a copy of the GNU General Public License
#along with this program.  If not, see <http://www.gnu.org/licenses/>.
######################################################################
# ip_catalog availability surfaces (aurora2 #2246).
#
# Runs against the real binary and the real dummy IP catalog on disk, including
# the ip_manifest.json files that sit beside dsp_generator/ and preview_ip/.

set platform $::tcl_platform(platform)
if { $platform == "windows" } {
    puts "SKIPPING ON WINDOWS: This test requires python which FileUtils::ExecuteSystemCommand() currently fails to find on Windows.\n"
    exit 0
}

proc fail { msg } {
    puts "TEST FAILED: $msg"
    exit 1
}

create_design ip_availability_test
architecture ../../Arch/k6_frac_N10_tileable_40nm.xml ../../Arch/k6_N10_40nm_openfpga.xml
add_litex_ip_catalog ./IP_Catalog

# 1. The bare command must stay a flat, space-separated list of names.
#    Shipped testcases and user scripts do `foreach ip [ip_catalog]` on it.
set names [ip_catalog]
if { [string first "\n" $names] >= 0 } {
    fail "ip_catalog must return a flat space-separated list, got:\n$names"
}
foreach expected {axis_converter_V1_0 dsp_generator_v1_0 dsp_generator_v2_0 preview_ip_V1_0} {
    if { [lsearch -exact $names $expected] < 0 } {
        fail "ip_catalog did not list $expected: $names"
    }
}

# 2. -all annotates every IP with a state and a reason.
set all [ip_catalog -all]
if { ![regexp {axi_ram_V1_0 \[production\] available on all devices\.} $all] } {
    fail "ip_catalog -all did not report an ungated IP as production:\n$all"
}
if { ![regexp {preview_ip_V1_0 \[preview\] .*Preview IP, not production-qualified: timing is not characterised yet\.} $all] } {
    fail "ip_catalog -all did not report the preview IP and its note:\n$all"
}
# No device is selected in this batch run, so the fabric gate must not be
# evaluated against a default-constructed target - it must say so instead.
if { ![regexp {dsp_generator_v2_0 \[production\] requires DSPV2 fabric; no device is selected, so availability is not evaluated\.} $all] } {
    fail "ip_catalog -all did not annotate the DSPV2 IP with the no-device reason:\n$all"
}

# 3. -format json carries the same facts in a machine-readable form.
set records [ip_catalog -format json]
foreach key {"name" "state" "maturity" "available" "listed" "reason"} {
    if { [string first "\"$key\"" $records] < 0 } {
        fail "ip_catalog -format json is missing the $key field:\n$records"
    }
}
if { [string first "\"preview_ip_V1_0\"" $records] < 0 } {
    fail "ip_catalog -format json did not report preview_ip_V1_0:\n$records"
}

# 4. Bad options are rejected rather than silently ignored.
if { ![catch {ip_catalog -nosuchoption}] } {
    fail "ip_catalog accepted an unknown option"
}

puts "TEST PASSED: ip_catalog availability surfaces"
exit 0

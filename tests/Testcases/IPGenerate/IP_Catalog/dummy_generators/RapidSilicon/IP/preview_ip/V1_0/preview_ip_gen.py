#!/usr/bin/env python3
"""Dummy generator for the IP availability model (aurora2 #2246).

Stands in for a real litex generator, like the other dummy generators here,
but unlike them it also honours --build so the wrapper-stamping half of the
preview flow can be exercised end to end. The output layout mirrors what the
shipped generators produce (see IP_Catalog/quicklogic/lib/common.py):

    <build_dir>/<vendor>/<library>/<name>/<version>/<build_name>/src/
        <build_name>_<version>.v
"""
import argparse
import json
import os

VENDOR = "RapidSilicon"
LIBRARY = "IP"
NAME = "preview_ip"
VERSION = "V1_0"

TEMPLATE = """// dummy preview IP wrapper
module {build_name} (
    input  wire clk,
    input  wire d,
    output reg  q
);
    always @(posedge clk) q <= d;
endmodule
"""


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--json_template", action="store_true")
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--json")
    args, _ = parser.parse_known_args()

    if not args.build:
        print(json.dumps({
            "build_dir": "build",
            "build_name": "preview_ip_wrapper",
            "build": False,
            "json": None,
            "json_template": False,
        }))
        return

    with open(args.json) as handle:
        config = json.load(handle)
    build_dir = config["build_dir"]
    build_name = config["build_name"]

    src_dir = os.path.join(build_dir, VENDOR, LIBRARY, NAME, VERSION,
                           build_name, "src")
    os.makedirs(src_dir, exist_ok=True)
    wrapper = os.path.join(src_dir, build_name + "_" + VERSION + ".v")
    with open(wrapper, "w") as handle:
        handle.write(TEMPLATE.format(build_name=build_name))
    print("generated " + wrapper)


main()

#!/usr/bin/env python3
import json

# Simulated output of `python3 <some_generator>_gen.py --json_template`, the
# same stand-in the other dummy generators use. This one exists to give the IP
# availability model (aurora2 #2246) a real catalog entry with a real
# ip_manifest.json beside it.

obj = {
    "build_dir": "build",
    "build_name": "preview_ip_wrapper",
    "build": False,
    "json": None,
    "json_template": False
}

print( json.dumps(obj) )

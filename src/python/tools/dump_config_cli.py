#!/usr/bin/env python3
import sys
import json
import pyYARR


def dump_config():
    # Parse the scanConsole command line (same options as scan_cli.py) and
    # return the resulting option/config JSON instead of running the scan.
    cfg = pyYARR.parseConfig(sys.argv)
    # Pretty-print the JSON string returned by parseConfig
    print(json.dumps(json.loads(cfg), indent=4))
    return 0


if __name__ == '__main__':
    sys.exit(dump_config())

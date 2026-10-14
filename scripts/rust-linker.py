#!/usr/bin/env python3
"""Adapt rustc's GNU-driver flags for Zig's direct lld entrypoint."""
import os
import shutil
import sys

args = []
for item in sys.argv[1:]:
    if item in ("-nostartfiles", "-nodefaultlibs"):
        continue
    if item.startswith("-Wl,"):
        args.extend(item[4:].split(","))
    else:
        args.append(item)
zig = os.environ.get("ZIG") or shutil.which("zig")
if not zig:
    raise SystemExit("ZIG or zig is required for the aarch64 musl linker")
os.execvp(zig, [zig, "ld.lld", *args])

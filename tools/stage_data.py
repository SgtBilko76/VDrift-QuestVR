#!/usr/bin/env python3
"""
stage_data.py - build the on-device data tree for VDrift VR.

Copies the VDrift data checkout (E:/vdrift/vdrift-data, an SVN working copy of
https://svn.code.sf.net/p/vdrift/code/vdrift-data) into

    E:/VDriftVR/stage/VDriftVR/data/...

skipping SVN metadata, build files and the test data, then overlays
E:/VDriftVR/templates on the stage root (vr.cfg, .vdrift/controls.config).

Usage: python tools/stage_data.py [--clean] [--minimal] [--cars A,B] [--tracks X,Y]
  --minimal   only the cars and tracks VDrift's own "minimal" install ships
  --cars/--tracks  an explicit subset (directory names), for a quicker loop
"""
import argparse
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.environ.get("VDRIFT_DATA", os.path.join(os.path.dirname(ROOT), "vdrift", "vdrift-data"))
STAGE = os.path.join(ROOT, "stage", "VDriftVR")
OVERLAY = os.path.join(ROOT, "templates")

SKIP_DIRS = {".svn", "test", "__pycache__"}
SKIP_FILES = {"SConscript", "SConscript.no_data", "Makefile", ".cvsignore"}
SKIP_EXT = {".xcf", ".blend", ".blend1", ".psd", ".bak", ".o", ".obj", ".pyc"}

# VDrift's "minimal" install (SConstruct minimal=1): three cars and two tracks.
MINIMAL_CARS = {"XS", "TL2", "F1", "TC6", "360"}
MINIMAL_TRACKS = {"paulricard88", "weekend", "estoril88"}

copied = 0


def wanted(name):
    if name in SKIP_FILES:
        return False
    return os.path.splitext(name)[1].lower() not in SKIP_EXT


def copy_file(src, dst):
    global copied
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(src) and \
            int(os.path.getmtime(dst)) >= int(os.path.getmtime(src)):
        return
    shutil.copy2(src, dst)
    copied += 1


def copy_tree(src, dst, keep_top=None):
    """Copy src into dst. keep_top: if given, only these top-level subdirs (plus files)."""
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        rel = os.path.relpath(dirpath, src)
        if keep_top is not None and rel != ".":
            top = rel.split(os.sep)[0]
            if top not in keep_top:
                dirnames[:] = []
                continue
        for f in filenames:
            if not wanted(f):
                continue
            copy_file(os.path.join(dirpath, f), os.path.join(dst, rel, f))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--clean", action="store_true", help="remove the stage dir first")
    ap.add_argument("--minimal", action="store_true", help="only the minimal car/track set")
    ap.add_argument("--cars", help="comma separated car directory names")
    ap.add_argument("--tracks", help="comma separated track directory names")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(DATA, "settings")):
        sys.exit("VDrift data not found at %s (set VDRIFT_DATA)" % DATA)

    if args.clean and os.path.isdir(STAGE):
        print("removing", STAGE)
        shutil.rmtree(STAGE)

    cars = set(args.cars.split(",")) if args.cars else (MINIMAL_CARS if args.minimal else None)
    tracks = set(args.tracks.split(",")) if args.tracks else (MINIMAL_TRACKS if args.minimal else None)

    data_dst = os.path.join(STAGE, "data")
    for entry in sorted(os.listdir(DATA)):
        src = os.path.join(DATA, entry)
        if entry in SKIP_DIRS:
            continue
        if os.path.isfile(src):
            if wanted(entry):
                copy_file(src, os.path.join(data_dst, entry))
            continue
        if entry == "cars":
            copy_tree(src, os.path.join(data_dst, entry), keep_top=cars)
        elif entry == "tracks":
            copy_tree(src, os.path.join(data_dst, entry), keep_top=tracks)
        else:
            copy_tree(src, os.path.join(data_dst, entry))
        print("  staged", entry)

    # VR overlay: vr.cfg and the .vdrift defaults, kept under templates/ on the
    # device too so the app can re-seed a wiped settings dir.
    copy_tree(OVERLAY, os.path.join(STAGE, "templates"))
    copy_file(os.path.join(OVERLAY, "vr.cfg"), os.path.join(STAGE, "vr.cfg"))

    total = 0
    for dirpath, _, filenames in os.walk(STAGE):
        for f in filenames:
            total += os.path.getsize(os.path.join(dirpath, f))
    print("staged %d new/changed files; %s is %.0f MB" % (copied, STAGE, total / 1048576.0))


if __name__ == "__main__":
    main()

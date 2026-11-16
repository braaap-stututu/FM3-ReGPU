"""Packs the Linux build (installer/build-linux.ps1): python -I installer/pack_linux.py <stage folder> <out .tar.gz>.
Written with Python's tarfile so the launcher and the start script keep their executable bit (Windows' tar can't set
it). Everything goes under one top folder, FM3-ReGPU/."""
import os
import sys
import tarfile

stage, out = sys.argv[1], sys.argv[2]
executable = {"fm3-launcher.sh", "launcher/FM3Launcher", "launcher/createdump"}
with tarfile.open(out, "w:gz") as tar:
    for dirpath, dirnames, filenames in os.walk(stage):
        dirnames.sort()
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, stage).replace(os.sep, "/")
            info = tar.gettarinfo(full, "FM3-ReGPU/" + rel)
            info.mode = 0o755 if rel in executable else 0o644
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            with open(full, "rb") as f:
                tar.addfile(info, f)
print(out)

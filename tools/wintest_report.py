#!/usr/bin/env python3
"""Summarize a tools/wintest.sh run: exit code, the test driver's log lines,
a symbolized crash backtrace, and a contact sheet of the captures.

  tools/wintest_report.py NAME [--nm x86_64-w64-mingw32-nm]

Symbols come from build-win/melee.exe, so report before rebuilding. The
contact sheet (test-runs/NAME/sheet.png) needs Pillow; it is skipped without.
"""
import argparse
import bisect
import os
import re
import shutil
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument("name")
ap.add_argument("--nm", default=shutil.which("x86_64-w64-mingw32-nm")
                or os.path.expanduser("~/.local/toolchains/mingw/bin/x86_64-w64-mingw32-nm"))
ap.add_argument("--exe", default=str(root / "build-win" / "melee.exe"))
args = ap.parse_args()
run = root / "test-runs" / args.name

code = (run / "exit_code").read_text().strip() if (run / "exit_code").exists() else "missing"
names = {"0": "ok", "3": "test FAIL", "-1073741819": "access violation",
         "-1073740791": "stack overrun / abort", "-1073741571": "stack overflow"}
print(f"exit code: {code} ({names.get(code, '?')})")

log = (run / "melee-pc.log").read_text(errors="replace") if (run / "melee-pc.log").exists() else ""
for line in log.splitlines():
    if "test:" in line or "[8css] left" in line:
        print("  " + line.split("] ", 1)[-1])

crash = [l for l in log.splitlines() if l.startswith("[FATAL]")]
if crash:
    syms = []
    out = subprocess.run([args.nm, "-n", args.exe], capture_output=True, text=True).stdout
    for l in out.splitlines():
        parts = l.split()
        if len(parts) == 3 and parts[1] in "tT":
            syms.append((int(parts[0], 16), parts[2]))
    addrs = [a for a, _ in syms]
    print("crash:")
    for l in crash[:2]:
        print("  " + l[8:])
    for l in crash:
        m = re.match(r"\[FATAL\]\s+#(\d+) ([0-9a-f]+) melee\.exe", l)
        if m:
            a = int(m.group(2), 16)
            i = bisect.bisect_right(addrs, a) - 1
            sym = f"{syms[i][1]}+0x{a - syms[i][0]:x}" if i >= 0 else "?"
            print(f"  #{m.group(1)} {sym}")

shots = sorted((run / "captures").glob("*.png"))
print(f"captures: {len(shots)}")
try:
    from PIL import Image, ImageDraw
except ImportError:
    Image = None
if shots and Image:
    cols = min(3, len(shots))
    w, h = 426, 320
    rows = (len(shots) + cols - 1) // cols
    sheet = Image.new("RGB", (w * cols, h * rows))
    for i, f in enumerate(shots):
        im = Image.open(f).convert("RGB").resize((w, h))
        ImageDraw.Draw(im).text((4, 4), f.stem, fill=(255, 255, 0))
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(run / "sheet.png")
    print(f"sheet: {run / 'sheet.png'}")

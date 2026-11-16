"""NoteView for the Casio fx-CG50: convert photos on the PC and upload them to the calculator.

Run `python convert.py` (or double-click upload.bat on Windows); -h lists the options.
Each photo is EXIF-rotated and scaled to the screen width (396 px). Wide photos are
cropped at the sides to fill the screen; taller ones keep their full height
(up to 5 screens) and are scrolled with UP/DOWN on the calculator.

.img format: see src/view.h. Level 1 = 396 px wide, level 2 = 792 px wide (zoom
detail), both cut in 112-row JPEG strips so the calculator only decodes what is on screen.
--small (or photos under 792 px wide) leaves out level 2: smaller, zoom is blocky.
--contrast is for photos of notebook pages: white paper, darker ink, shadows removed.
"""
import argparse
import glob
import io
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter, ImageOps

W, H = 396, 224
STRIP, MAX_H = 112, 1120  # same as src/view.h
HERE = Path(__file__).parent
EXTS = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".webp", ".tif", ".tiff"}
MAX_FILE = 256 * 1024  # the add-in's read buffer (MAX_FILE in src/view.h)
LANCZOS = Image.Resampling.LANCZOS
GAINS = {"low": 1.8, "normal": 2.5, "high": 3.2}  # ink gain of clean(); "none" = no cleanup
MANIFEST, ALGO = ".manifest.json", 1  # bump ALGO when convert() output changes: forces reconversion
G3A = HERE / "NoteView.g3a"

HELP = """NoteView for the Casio fx-CG50: converts your photos and puts them on the calculator.

  1. Put photos (jpg, png, bmp, gif, webp, tif) in the "photos" folder next to this
     script. One level of subfolders becomes folders on the calculator.
  2. Connect the calculator with the USB cable and press F1 "USB Flash" on it.
  3. Run this tool: it converts, copies to the calculator and ejects it.
"""


def jpeg(im, quality):
    out = io.BytesIO()
    im.save(out, "JPEG", quality=quality, optimize=True)  # baseline, 4:2:0: what TJpgDec decodes
    return out.getvalue()


def clean(im, gain=GAINS["normal"]):
    """Notebook pages: white paper, darker ink, no shadows. Keeps pen colours."""
    # paper brightness everywhere = blur of a max filter, which removes the (thin, dark) ink
    paper = im.reduce(4).filter(ImageFilter.MaxFilter(9)).filter(ImageFilter.GaussianBlur(8))
    ink = ImageChops.subtract(paper.resize(im.size, Image.Resampling.BILINEAR), im)  # darker than paper, per channel
    return ImageChops.invert(ink.point(lambda v: 0 if v < 12 else min(255, int((v - 12) * gain))))


def convert(src, small=False, contrast="normal"):
    cleanup = contrast != "none"
    im = ImageOps.exif_transpose(Image.open(src)).convert("RGB")
    h = min(max(round(W * im.height / im.width / 16) * 16, H), MAX_H)
    small = small or im.width < 2 * W  # nothing extra to keep for zoom
    # centering (0.5, 0): crop wide photos at both sides, very tall ones at the bottom
    if small and not cleanup:
        levels = [ImageOps.fit(im, (W, h), LANCZOS, centering=(0.5, 0))]
    else:
        lv2 = ImageOps.fit(im, (2 * W, 2 * h), LANCZOS, centering=(0.5, 0))
        if cleanup:
            lv2 = clean(lv2, GAINS[contrast])
        # pages: strokes 1 px bolder before halving, or thin pencil fades at 1x
        lv1 = (lv2.filter(ImageFilter.MinFilter(3)) if cleanup else lv2).resize((W, h), LANCZOS)
        levels = [lv1] if small else [lv1, lv2]
    for q in (90, 80, 70, 60, 50):  # very detailed photos: lower quality until the file fits
        strips = [jpeg(lv.crop((0, y, lv.width, min(y + STRIP, lv.height))), q)
                  for lv in levels for y in range(0, lv.height, STRIP)]
        n1 = -(-h // STRIP)
        offs = [12 + 4 * (len(strips) + 1)]
        for s in strips:
            offs.append(offs[-1] + len(s))
        if offs[-1] <= MAX_FILE:
            head = b"CGI2" + b"".join(v.to_bytes(2, "big") for v in (h, n1, len(strips) - n1, 0))
            return head + b"".join(o.to_bytes(4, "big") for o in offs) + b"".join(strips)
    raise ValueError("too detailed even at quality 50")


def out_name(stem, used, ext=".img"):
    # ASCII only: the calculator font has no other characters
    base = re.sub(r"[^A-Za-z0-9_-]+", "_", stem).strip("_")[:24] or "photo"
    name, i = base, 2
    while name.lower() in used:
        name, i = f"{base}_{i}", i + 1
    used.add(name.lower())
    return name + ext


def convert_all(src_dir, dst_dir, small=False, contrast="normal"):
    """photos/Math/a.jpg -> converted/Math/a.img. Returns the number of images (0 if there are none)."""
    photos = sorted(p for p in src_dir.rglob("*") if p.is_file() and p.suffix.lower() in EXTS
                    and not any(x.startswith(".") for x in p.relative_to(src_dir).parts))
    if not photos:
        print(f"No photos in {src_dir}")
        return 0
    dst_dir.mkdir(parents=True, exist_ok=True)
    try:
        old = json.loads((dst_dir / MANIFEST).read_text())
    except (OSError, ValueError):
        old = {}
    opts = f"v{ALGO} small={int(small)} contrast={contrast}"
    new, folders, names, fmap = {}, set(), {}, {}  # new: out path -> [photo path, opts]
    for p in photos:
        rel = p.relative_to(src_dir)
        top = rel.parts[0] if len(rel.parts) > 1 else ""  # deeper levels go into their first-level folder
        if top not in fmap:
            fmap[top] = out_name(top, folders, "") if top else ""
        out = out_name(p.stem, names.setdefault(fmap[top], set()))
        key = f"{fmap[top]}/{out}" if top else out
        entry = [rel.as_posix(), opts]
        path = dst_dir / key
        if old.get(key) == entry and path.is_file() and path.stat().st_mtime >= p.stat().st_mtime:
            print(f"{rel} -> {key} (unchanged)")
        else:
            try:
                data = convert(p, small, contrast)
            except Exception as e:  # one broken file should not stop the rest
                print(f"skip {rel}: {e}")
                continue
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(data)
            print(f"{rel} -> {key}")
        new[key] = entry
    for key in old.keys() - new.keys():  # photos that were removed or renamed
        (dst_dir / key).unlink(missing_ok=True)
        try:
            (dst_dir / key).parent.rmdir()  # only if empty
        except OSError:
            pass
    (dst_dir / MANIFEST).write_text(json.dumps(new, indent=1), newline="\n")
    total = sum((dst_dir / k).stat().st_size for k in new)
    print(f"{len(new)} images, {total / 1024 / 1024:.1f} MB in {dst_dir} (calculator storage is about 16 MB)")
    return len(new)


def calculator_roots():
    """Places where a USB-mounted calculator can show up."""
    if sys.platform == "win32":
        import ctypes
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        return [Path(f"{chr(65 + i)}:/") for i in range(2, 26) if mask >> i & 1]
    pats = ["/Volumes/*"] if sys.platform == "darwin" else ["/media/*/*", "/run/media/*/*", "/media/*", "/mnt/*"]
    return [Path(m) for pat in pats for m in sorted(glob.glob(pat))]


def find_calculator(roots):
    """First root that has an @MainMem folder (the calculator in USB Flash mode)."""
    return next((Path(r) for r in roots if os.path.isdir(os.path.join(r, "@MainMem"))), None)


def copy_to(target, dst_dir, g3a=None):
    """Copy g3a and every .img under dst_dir (keeping folders) into target; files that are already
    identical there are skipped. Never deletes anything. Returns (copied, same) as lists of paths."""
    files = [(f, f.relative_to(dst_dir).as_posix()) for f in sorted(dst_dir.rglob("*.img"))]
    if g3a and Path(g3a).is_file():
        files.insert(0, (Path(g3a), Path(g3a).name))
    copied, same = [], []
    for src, rel in files:
        dst = Path(target) / rel
        if dst.is_file() and dst.stat().st_size == src.stat().st_size and dst.read_bytes() == src.read_bytes():
            same.append(rel)
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src, dst)
        copied.append(rel)
    return copied, same


def eject(root):
    """Unmount so the calculator finishes writing before you unplug. True if the request went through."""
    root = str(root)
    try:
        if sys.platform == "win32":  # Explorer's "Eject"
            ps = f"(New-Object -ComObject Shell.Application).Namespace(17).ParseName('{root}').InvokeVerb('Eject')"
            subprocess.run(["powershell", "-NoProfile", "-Command", ps], check=True)
        elif sys.platform == "darwin":
            subprocess.run(["diskutil", "eject", root], check=True)
        else:  # shortcut: needs udisksctl (most desktop Linux); otherwise the caller tells the user to eject
            dev = subprocess.run(["findmnt", "-n", "-o", "SOURCE", "--target", root],
                                 capture_output=True, text=True, check=True).stdout.strip()
            subprocess.run(["udisksctl", "unmount", "-b", dev], check=True)
            subprocess.run(["udisksctl", "power-off", "-b", dev], check=True)
        return True
    except (OSError, subprocess.CalledProcessError):
        return False


def upload(dst_dir, roots=None, g3a=G3A, do_eject=True):
    """Find the calculator, copy, eject. Returns False if no calculator was found."""
    root = find_calculator(calculator_roots() if roots is None else roots)
    if not root:
        print("\nNo calculator found, so nothing was uploaded.\n"
              'Connect it with the USB cable, press F1 "USB Flash" on the calculator and run this again.\n'
              f"The converted files are kept in {dst_dir}; the next run only converts what changed.")
        return False
    print(f"\nCalculator found: {root}")
    copied, same = copy_to(root, dst_dir, g3a)
    for rel in copied:
        print(f"copied {rel}")
    print(f"{len(copied)} copied, {len(same)} already on the calculator")
    if do_eject and eject(root):
        print('Eject requested. Wait for the calculator to finish "Updating", then unplug.')
    elif do_eject:
        print("Could not eject automatically: eject the drive safely in your system, then unplug.")
    else:
        print('Wait for the calculator to finish "Updating", then unplug.')
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(prog="convert.py", description=HELP, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input_dir", nargs="?", type=Path, default=HERE / "photos",
                    help="photos (and subfolders) to convert (default: the \"photos\" folder next to this script)")
    ap.add_argument("output_dir", nargs="?", type=Path, default=HERE / "converted",
                    help="where the .img files go (default: the \"converted\" folder next to this script)")
    ap.add_argument("--contrast", choices=[*GAINS, "none"], default="normal",
                    help="cleanup for photos of notebook pages: white paper, darker ink, no shadows; "
                         "none = leave photos as they are (default: %(default)s)")
    ap.add_argument("--small", action="store_true",
                    help="no zoom detail: files about 5x smaller, zoom is blocky (default: off)")
    ap.add_argument("--no-upload", action="store_true",
                    help="only convert, do not touch the calculator (default: upload if one is connected)")
    a = ap.parse_args(argv)
    print(ap.format_help())  # -h exits in parse_args, so it is not printed twice
    if not convert_all(a.input_dir, a.output_dir, a.small, a.contrast):
        return 1
    if a.no_upload:
        return 0
    try:
        upload(a.output_dir)
    except OSError as e:  # e.g. calculator full or unplugged
        print(f"Upload failed: {e}")
        return 1
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(errors="replace")  # photo names with odd characters on a cp1252 console
    sys.exit(main())

"""Puts the screenshots in docs/ into the pictures in docs/art/ (their screens are pure magenta #FF00FF).

- art/calculator.png + every screenshot docs/<name>.png -> docs/mockup-<name>.jpg
  (cropped to the screen and the top keys, 600 px wide)
- art/banner.png and art/social.png + docs/page.png -> docs/banner.jpg, docs/social.jpg
Run: python docs/make_mockups.py
"""
from pathlib import Path

from PIL import Image, ImageChops, ImageFilter

DOCS = Path(__file__).parent
OUTPUTS = ("mockup-", "banner", "social")


def screen(frame):
    """Mask and bounding box of the magenta screen (slightly grown to cover anti-aliased edges)."""
    r, g, b = frame.split()
    mask = ImageChops.multiply(ImageChops.multiply(r.point(lambda v: 255 if v > 180 else 0),
                                                   b.point(lambda v: 255 if v > 180 else 0)),
                               g.point(lambda v: 255 if v < 90 else 0)).filter(ImageFilter.MaxFilter(5))
    box = mask.getbbox()
    assert box, "no magenta screen found"
    return mask, box


def put(frame, shot):
    mask, box = screen(frame)
    w, h = box[2] - box[0], box[3] - box[1]
    out = frame.copy()
    out.paste(Image.open(shot).convert("RGB").resize((w, h), Image.Resampling.LANCZOS), box[:2], mask.crop(box))
    return out, box


calc = Image.open(DOCS / "art" / "calculator.png").convert("RGB")
for shot in sorted(DOCS.glob("*.png")):
    if shot.name.startswith(OUTPUTS):
        continue
    out, box = put(calc, shot)
    h = box[3] - box[1]
    top, bottom = max(0, box[1] - h // 4), min(out.height, box[3] + int(h * 0.75))
    out = out.crop((0, top, out.width, bottom))
    out.resize((600, round(600 * out.height / out.width)), Image.Resampling.LANCZOS).save(
        DOCS / f"mockup-{shot.stem}.jpg", quality=88)
    print("wrote", f"mockup-{shot.stem}.jpg")

for name in ("banner", "social"):
    out, box = put(Image.open(DOCS / "art" / f"{name}.png").convert("RGB"), DOCS / "page.png")
    out.save(DOCS / f"{name}.jpg", quality=88)
    print("wrote", f"{name}.jpg", "screen", box)

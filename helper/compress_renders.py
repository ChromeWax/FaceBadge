from PIL import Image
from pathlib import Path
import struct
import zlib
import random

BG = (0, 0, 0)
COLORS = 16

pngs = sorted(Path("renders").glob("*.png"))
print(f"Found {len(pngs)} source PNGs")

# Build shared palette from a random sample of frames
sample = random.sample(pngs, min(50, len(pngs)))
composite_w = 240 * len(sample)
composite = Image.new("RGB", (composite_w, 320))
for i, png in enumerate(sample):
    img = Image.open(png).convert("RGBA")
    bg_img = Image.new("RGB", img.size, BG)
    bg_img.paste(img, mask=img.split()[3])
    composite.paste(bg_img, (i * 240, 0))
shared_q = composite.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT)
shared_palette_pil = shared_q.getpalette()[:COLORS * 3]
print(f"Built shared {COLORS}-color palette from {len(sample)} sample frames")

# Convert palette to RGB565
rgb565_palette = []
for j in range(COLORS):
    r, g, b = shared_palette_pil[j * 3], shared_palette_pil[j * 3 + 1], shared_palette_pil[j * 3 + 2]
    rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    rgb565_palette.append(rgb565)

Path("raw_renders").mkdir(exist_ok=True)

for i, png in enumerate(pngs):
    img = Image.open(png).convert("RGBA")
    bg_img = Image.new("RGB", img.size, BG)
    bg_img.paste(img, mask=img.split()[3])
    q = bg_img.quantize(palette=shared_q, dither=Image.Dither.NONE)
    indices = q.get_flattened_data()
    compressed = zlib.compress(bytes(indices), 9)

    raw_path = f"raw_renders/{png.stem}.raw"
    with open(raw_path, "wb") as f:
        f.write(struct.pack("<H", COLORS))
        for c in rgb565_palette:
            f.write(struct.pack("<H", c))
        f.write(struct.pack("<H", len(compressed)))
        f.write(compressed)

    if i % 20 == 0:
        print(f"  [{i+1}/{len(pngs)}] {raw_path} ({len(compressed)} bytes)")

print(f"Done — {len(pngs)} images converted to raw_renders/")
import argparse
from PIL import Image
from pathlib import Path
import struct
import zlib
import random

parser = argparse.ArgumentParser(description="Compress render PNGs into .raw files")
parser.add_argument("input_dir", type=str, help="Directory containing *.png renders")
parser.add_argument("output_dir", type=str, help="Directory to write .raw files to")
args = parser.parse_args()

BG = (0, 0, 0)
COLORS = 16

input_dir = Path(args.input_dir)
output_dir = Path(args.output_dir)
pngs = sorted(input_dir.glob("*.png"))
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

output_dir.mkdir(exist_ok=True)

for i, png in enumerate(pngs):
    img = Image.open(png).convert("RGBA")
    bg_img = Image.new("RGB", img.size, BG)
    bg_img.paste(img, mask=img.split()[3])
    q = bg_img.quantize(palette=shared_q, dither=Image.Dither.NONE)
    indices = q.get_flattened_data()
    compressed = zlib.compress(bytes(indices), 9)

    raw_path = output_dir / f"{png.stem}.raw"
    with open(raw_path, "wb") as f:
        f.write(struct.pack("<H", COLORS))
        for c in rgb565_palette:
            f.write(struct.pack("<H", c))
        f.write(struct.pack("<H", len(compressed)))
        f.write(compressed)

    if i % 20 == 0:
        print(f"  [{i+1}/{len(pngs)}] {raw_path} ({len(compressed)} bytes)")

print(f"Done — {len(pngs)} images converted to {output_dir}/")
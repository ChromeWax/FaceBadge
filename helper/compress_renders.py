from PIL import Image
from pathlib import Path

BG = (0, 0, 0)  # #000

Path("compressed_renders").mkdir(exist_ok=True)

pngs = sorted(Path("renders").glob("*.png"))
for i, png in enumerate(pngs):
    img = Image.open(png).convert("RGBA")
    bg = Image.new("RGB", img.size, BG)
    bg.paste(img, mask=img.split()[3])
    jpg = f"compressed_renders/{png.stem}.jpg"
    bg.save(jpg, "JPEG", quality=70, optimize=True)
    if i % 20 == 0:
        print(f"  [{i+1}/{len(pngs)}] {jpg}")

print(f"Done — {len(pngs)} images converted to compressed_renders/")

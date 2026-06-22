from PIL import Image
import os

RENDERS_DIR = "images/renders"
COMPRESSED_DIR = "images/compressed_renders"

W, H = 120, 160
count = 0

for fname in os.listdir(RENDERS_DIR):
    if not fname.endswith(".png"):
        continue
    jpg_name = fname.replace(".png", ".jpg")
    src = os.path.join(RENDERS_DIR, fname)
    dst = os.path.join(COMPRESSED_DIR, jpg_name)

    img = Image.open(src).resize((W, H), Image.LANCZOS)
    img = img.convert("RGB")
    img.save(dst, quality=70, optimize=True)
    count += 1

print(f"Resized and saved {count} images to {W}x{H}")

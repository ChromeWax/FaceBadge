from PIL import Image
import os, re

COMPRESSED_DIR = "images/compressed_renders"
OUTPUT_PATH = os.path.join(COMPRESSED_DIR, "atlas.jpg")

pitch_values = list(range(-10, 11, 2))
yaw_values = list(range(-20, 21, 2))

CELL_W, CELL_H = 240, 320
COLS, ROWS = len(yaw_values), len(pitch_values)
ATLAS_W, ATLAS_H = COLS * CELL_W, ROWS * CELL_H

pitch_to_row = {p: i for i, p in enumerate(pitch_values)}
yaw_to_col = {y: j for j, y in enumerate(yaw_values)}

atlas = Image.new("RGB", (ATLAS_W, ATLAS_H), (255, 255, 255))
pattern = re.compile(r"pitch_([+-]\d+)_yaw_([+-]\d+)\.jpg")

for fname in os.listdir(COMPRESSED_DIR):
    m = pattern.match(fname)
    if not m:
        continue
    pitch, yaw = int(m.group(1)), int(m.group(2))
    row, col = pitch_to_row.get(pitch), yaw_to_col.get(yaw)
    if row is None or col is None:
        continue
    img = Image.open(os.path.join(COMPRESSED_DIR, fname))
    atlas.paste(img, (col * CELL_W, row * CELL_H))

atlas.save(OUTPUT_PATH, quality=95)
print(f"Saved atlas: {ATLAS_W}x{ATLAS_H}px ({ROWS} pitch rows x {COLS} yaw cols)")

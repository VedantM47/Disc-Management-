import sys, glob, re
from PIL import Image
files = sorted(glob.glob(sys.argv[1] + "/Slide*.PNG"), key=lambda f: int(re.findall(r"(\d+)", f)[-1]))
w, h = 640, 360; cols = int(sys.argv[3]) if len(sys.argv) > 3 else 4
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (w + 10) + 10, rows * (h + 10) + 10), "white")
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize((w, h))
    sheet.paste(im, (10 + (i % cols) * (w + 10), 10 + (i // cols) * (h + 10)))
sheet.save(sys.argv[2], quality=85)
print(len(files), "slides ->", sys.argv[2])

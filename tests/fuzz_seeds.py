#!/usr/bin/env python3
"""Genera en build/fuzz/seeds archivos validos (JPEG base/progresivo, PNG, GIF, BMP, zlib/gzip/deflate) para tests/fuzz_decoders.c."""
import gzip, io, os, random, sys, zlib
from PIL import Image
out = sys.argv[1] if len(sys.argv) > 1 else "build/fuzz/seeds"
os.makedirs(out, exist_ok=True)
random.seed(1)
def img(w, h, mode):
    im = Image.new(mode, (w, h)); px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 7 % 256, y * 5 % 256, (x + y) * 3 % 256) if mode == "RGB" else
                        (x * 7 % 256, y * 5 % 256, (x + y) % 256, x * y % 256) if mode == "RGBA" else (x * y) % 256)
    return im
for name, fmt, kw, mode in [("a.jpg", "JPEG", {}, "RGB"), ("b.jpg", "JPEG", {"progressive": True}, "RGB"),
                            ("c.jpg", "JPEG", {"subsampling": 0, "quality": 40}, "RGB"), ("g.jpg", "JPEG", {}, "L"),
                            ("d.png", "PNG", {}, "RGB"), ("e.png", "PNG", {}, "RGBA"), ("f.png", "PNG", {"optimize": True}, "L"),
                            ("p.png", "PNG", {}, "P"), ("h.gif", "GIF", {}, "P"), ("i.bmp", "BMP", {}, "RGB")]:
    im = img(37, 29, "RGB" if mode == "P" else mode)
    if mode == "P": im = im.convert("P")
    b = io.BytesIO(); im.save(b, fmt, **kw); open(os.path.join(out, name), "wb").write(b.getvalue())
raw = bytes(random.randrange(0, 16) for _ in range(3000))
open(os.path.join(out, "z.zlib"), "wb").write(zlib.compress(raw))
open(os.path.join(out, "z.gz"), "wb").write(gzip.compress(raw))
co = zlib.compressobj(9, zlib.DEFLATED, -15); open(os.path.join(out, "z.raw"), "wb").write(co.compress(raw) + co.flush())
print("semillas en", out)

#!/usr/bin/env python3
"""SOMA 022: genera JPEG de prueba y su decodificacion de referencia (PIL/libjpeg) en un directorio.
Uso: jpegref.py DIR   -> DIR/caso.jpg + DIR/caso.rgb (w,h en el nombre: caso_WxH_tol)."""
import sys, os, io, random
from PIL import Image
d = sys.argv[1]; os.makedirs(d, exist_ok=True)
rnd = random.Random(7)
def img(w, h, kind):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            if kind == "grad": px[x, y] = ((x * 255) // max(w - 1, 1), (y * 255) // max(h - 1, 1), ((x + y) * 255) // max(w + h - 2, 1))
            elif kind == "flat": px[x, y] = (200, 100, 50)
            else: px[x, y] = (rnd.randrange(256), rnd.randrange(256), rnd.randrange(256))
    return im
cases = []
for (w, h) in [(8, 8), (16, 16), (33, 17), (64, 48), (100, 75), (1, 1), (7, 130)]:
    for kind in ("grad", "flat", "noise"):
        for ss, tag in ((0, "444"), (1, "422"), (2, "420")):
            for q in (50, 90):
                cases.append((w, h, kind, ss, tag, q, False))
cases.append((64, 48, "grad", 2, "420", 75, True))     # con intervalo de reinicio
cases.append((120, 90, "noise", 0, "444", 85, True))
n = 0
for (w, h, kind, ss, tag, q, rst) in cases:
    im = img(w, h, kind)
    b = io.BytesIO()
    kw = dict(quality=q, subsampling=ss)
    if rst: kw["restart_marker_blocks"] = 3
    im.save(b, "JPEG", **kw)
    data = b.getvalue()
    ref = Image.open(io.BytesIO(data)).convert("RGB")
    # tolerancia: 444 exacta salvo redondeo; con submuestreo, libjpeg usa filtro "fancy" -> solo se compara con tolerancia amplia en zonas suaves
    tol = 3 if ss == 0 else (255 if kind == "noise" else 40)
    name = f"c{n:03d}_{tag}_{kind}_{w}x{h}_{tol}"
    open(os.path.join(d, name + ".jpg"), "wb").write(data)
    open(os.path.join(d, name + ".rgb"), "wb").write(ref.tobytes())
    n += 1
# gris
for (w, h) in [(8, 8), (50, 33)]:
    im = img(w, h, "grad").convert("L"); b = io.BytesIO(); im.save(b, "JPEG", quality=80)
    data = b.getvalue(); ref = Image.open(io.BytesIO(data)).convert("RGB")
    name = f"c{n:03d}_gray_grad_{w}x{h}_3"
    open(os.path.join(d, name + ".jpg"), "wb").write(data); open(os.path.join(d, name + ".rgb"), "wb").write(ref.tobytes()); n += 1
# progresivo (debe rechazarse)
b = io.BytesIO(); img(32, 32, "grad").save(b, "JPEG", progressive=True)
open(os.path.join(d, "prog.jpg"), "wb").write(b.getvalue())
print(n, "casos en", d)

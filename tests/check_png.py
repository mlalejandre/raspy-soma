# PARCHE 083: comprobacion independiente de los PNG de test_png.c (zlib de Python y, si esta, PIL)
import zlib, struct, sys, os
d = sys.argv[1] if len(sys.argv) > 1 else "build/tests"
def png_pixels(path):
    b = open(path, "rb").read()
    assert b[:8] == b"\x89PNG\r\n\x1a\n"
    i, idat, w = 8, b"", None
    while i < len(b):
        n = struct.unpack(">I", b[i:i+4])[0]; t = b[i+4:i+8]; c = b[i+8:i+8+n]
        assert zlib.crc32(t + c) & 0xFFFFFFFF == struct.unpack(">I", b[i+8+n:i+12+n])[0], "CRC"
        if t == b"IHDR": w, h, bd, ct = struct.unpack(">IIBB", c[:10]); assert bd == 8 and ct == 2
        if t == b"IDAT": idat += c
        i += 12 + n
    raw = zlib.decompress(idat); st = w * 3; out = bytearray(); prev = bytearray(st)
    for y in range(h):
        f = raw[y*(st+1)]; row = bytearray(raw[y*(st+1)+1:(y+1)*(st+1)])
        for k in range(st):
            a = row[k-3] if k >= 3 else 0
            if f == 1: row[k] = (row[k] + a) & 255
            elif f == 2: row[k] = (row[k] + prev[k]) & 255
            else: assert f == 0
        out += row; prev = row
    return bytes(out)
for name in ("png_dibujo", "png_ruido", "png_degradado"):
    assert png_pixels(os.path.join(d, name + ".png")) == open(os.path.join(d, name + ".rgb"), "rb").read(), name
    try:
        from PIL import Image
        im = Image.open(os.path.join(d, name + ".png")); im.load(); assert im.tobytes() == open(os.path.join(d, name + ".rgb"), "rb").read()
    except ImportError:
        pass
assert zlib.decompress(open(os.path.join(d, "zlib_texto.z"), "rb").read()) == b"SOMA SOMA SOMA SOMA SOMA SOMA SOMA SOMA abcabcabcabc"
print("check_png: OK (zlib" + (" + PIL" if "PIL" in sys.modules else "") + ")")

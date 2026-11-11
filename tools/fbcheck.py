#!/usr/bin/env python3
"""SOMA_PARCHE_021_FPU: verifica capturas de pantalla de QEMU (monitor: screendump x.ppm) contra el patron de /bin/fbtest.elf.

  python3 tools/fbcheck.py captura.ppm 800 600

Fotograma A (toda la pantalla): R = x*255/(w-1), G = y*255/(h-1), B = (x^y)&255, y un rectangulo 200x100 en (100,50) de
color 0xFF8000. Se compara pixel a pixel. Salida 0 = correcto.
"""
from __future__ import annotations

import sys
from pathlib import Path


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    b = path.read_bytes()
    if b[:2] != b"P6":
        raise ValueError("no es un PPM P6")
    parts, pos = [], 2
    while len(parts) < 3:
        while b[pos:pos + 1] in (b" ", b"\n", b"\r", b"\t"):
            pos += 1
        if b[pos:pos + 1] == b"#":
            while b[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while b[pos:pos + 1] not in (b" ", b"\n", b"\r", b"\t"):
            pos += 1
        parts.append(int(b[start:pos]))
    pos += 1
    w, h, mx = parts
    if mx != 255:
        raise ValueError("solo 8 bits por canal")
    data = b[pos:pos + w * h * 3]
    if len(data) != w * h * 3:
        raise ValueError("PPM truncado")
    return w, h, data


def expected_frame(w: int, h: int) -> bytearray:
    """Fotograma esperado (RGB, 3 bytes por pixel)."""
    out = bytearray(w * h * 3)
    xs_r = [x * 255 // (w - 1) for x in range(w)]
    for y in range(h):
        g = y * 255 // (h - 1)
        row = bytearray(w * 3)
        for x in range(w):
            o = x * 3
            row[o] = xs_r[x]; row[o + 1] = g; row[o + 2] = (x ^ y) & 255
        out[y * w * 3:(y + 1) * w * 3] = row
    for y in range(50, 150):
        out[(y * w + 100) * 3:(y * w + 300) * 3] = b"\xff\x80\x00" * 200
    return out


def check(path: Path, w: int, h: int) -> list[str]:
    pw, ph, data = read_ppm(path)
    if (pw, ph) != (w, h):
        return [f"la captura es {pw}x{ph}, se esperaba {w}x{h}"]
    exp = expected_frame(w, h)
    if bytes(exp) == data:
        return []
    bad = sum(1 for i in range(0, len(exp), 3) if exp[i:i + 3] != data[i:i + 3])
    first = next(i // 3 for i in range(0, len(exp), 3) if exp[i:i + 3] != data[i:i + 3])
    return [f"{bad} pixeles distintos de {w * h}; el primero en ({first % w}, {first // w}): "
            f"esperado {tuple(exp[first * 3:first * 3 + 3])}, hay {tuple(data[first * 3:first * 3 + 3])}"]


if __name__ == "__main__":
    probs = check(Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]))
    for p in probs:
        print("FALLO:", p)
    if not probs:
        print(f"OK: {sys.argv[2]}x{sys.argv[3]} identico al patron esperado, pixel a pixel")
    raise SystemExit(1 if probs else 0)

#!/usr/bin/env python3
"""SOMA_PARCHE_020_AUDIO: verifica el audio que SOMA mando a QEMU (-audiodev wav,path=...).

  python3 tools/wavcheck.py captura.wav --pattern 44100        # el patron de 'audio test' (1 s a 44100 Hz)
  python3 tools/wavcheck.py captura.wav --reference ref.wav    # un WAV concreto (lo que dio 'audio play')

Busca en la captura el inicio de la referencia y comprueba que TODA ella aparece sin una sola muestra distinta, que
antes y despues solo hay silencio y que no se repite ni queda un eco. Salida 0 = correcto.
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def pattern_pcm(frames: int) -> bytes:
    """Patron determinista de 'audio test': L(i) = ((i*37) mod 20001) - 10000, R(i) = -L(i); 16 bits, estereo."""
    out = bytearray()
    for i in range(frames):
        l = (i * 37) % 20001 - 10000
        out += struct.pack("<hh", l, -l)
    return bytes(out)


def read_wav_pcm(path: Path) -> tuple[bytes, int, int]:
    """(datos PCM, frecuencia, canales). Tolera cabeceras con tamanos sin actualizar (QEMU muerto de golpe)."""
    b = path.read_bytes()
    if b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        raise ValueError("no es un WAV")
    pos, rate, ch = 12, 0, 0
    while pos + 8 <= len(b):
        cid, sz = b[pos:pos + 4], struct.unpack_from("<I", b, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt ":
            _fmt, ch, rate = struct.unpack_from("<HHI", b, body)
        elif cid == b"data":
            end = len(b) if sz in (0, 0xFFFFFFFF) or body + sz > len(b) else body + sz
            return b[body:end], rate, ch
        pos = body + sz + (sz & 1)
    raise ValueError("sin bloque data")


def build_wav(pcm: bytes, rate: int, channels: int) -> bytes:
    """WAV PCM de 16 bits (cabecera clasica de 44 bytes)."""
    blk = channels * 2
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate, rate * blk, blk, 16)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)


def mono_pcm(samples: int) -> bytes:
    return b"".join(struct.pack("<h", (i * 53) % 20011 - 10005) for i in range(samples))


def mono_as_stereo(mono: bytes) -> bytes:
    """Lo que debe salir al tocar un mono: cada muestra en los dos canales."""
    return b"".join(mono[i:i + 2] * 2 for i in range(0, len(mono), 2))


def check_multi(captured: bytes, refs: list[bytes], need: int = 64) -> list[str]:
    """Varias pistas seguidas en una captura: cada una completa y exacta, y SILENCIO en todo lo demas."""
    problems, pos, expected_end = [], 0, []
    cur = 0
    for k, ref in enumerate(refs):
        probe = ref[:need * 4]
        off = captured.find(probe, cur)
        while off != -1 and off % 4:
            off = captured.find(probe, off + 1)
        if off == -1:
            problems.append(f"pista {k + 1}: su inicio no aparece (despues del final de la anterior)")
            continue
        if captured[off:off + len(ref)] != ref:
            seg = captured[off:off + len(ref)]
            n = next((i for i in range(min(len(seg), len(ref))) if seg[i] != ref[i]), min(len(seg), len(ref)))
            problems.append(f"pista {k + 1}: difiere de la referencia en el byte {n} de {len(ref)}")
        if any(captured[cur:off]):
            problems.append(f"pista {k + 1}: hay sonido ajeno antes de ella")
        cur = off + len(ref)
    if any(captured[cur:]):
        problems.append("hay sonido DESPUES de la ultima pista (eco o repeticion): "
                        f"{sum(1 for x in captured[cur:] if x)} bytes no nulos")
    return problems


def check(captured: bytes, reference: bytes, need: int = 64) -> list[str]:
    """Devuelve la lista de problemas (vacia = correcto)."""
    problems = []
    probe = reference[:need * 4]
    off = captured.find(probe)
    # la captura puede tener el inicio alineado a 4 bytes; se busca a cualquier desplazamiento multiplo de 4
    while off != -1 and off % 4:
        off = captured.find(probe, off + 1)
    if off == -1:
        return ["el inicio de la referencia no aparece en la captura"]
    seg = captured[off:off + len(reference)]
    if seg != reference:
        n = next((i for i in range(min(len(seg), len(reference))) if seg[i] != reference[i]), min(len(seg), len(reference)))
        problems.append(f"la captura difiere de la referencia en el byte {n} de {len(reference)} (muestra {n // 4})")
    before, after = captured[:off], captured[off + len(reference):]
    if any(before):
        problems.append("hay sonido ANTES de la referencia")
    if any(after):
        problems.append("hay sonido DESPUES de la referencia (eco o repeticion): "
                        f"{sum(1 for x in after if x)} bytes no nulos")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("capture", type=Path)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--pattern", type=int, metavar="HZ", help="patron de 'audio test' a esa frecuencia (1 s)")
    g.add_argument("--reference", type=Path, help="WAV de referencia (PCM 16 bits estereo)")
    a = ap.parse_args()
    cap, rate, ch = read_wav_pcm(a.capture)
    print(f"captura: {len(cap)} bytes, {rate} Hz, {ch} canales")
    ref = pattern_pcm(a.pattern) if a.pattern else read_wav_pcm(a.reference)[0]
    probs = check(cap, ref)
    for p in probs:
        print("FALLO:", p)
    if not probs:
        print(f"OK: {len(ref)} bytes de referencia reproducidos sin una sola muestra distinta, silencio antes y despues")
    return 1 if probs else 0


if __name__ == "__main__":
    raise SystemExit(main())

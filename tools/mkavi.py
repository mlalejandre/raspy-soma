#!/usr/bin/env python3
"""SOMA 022: crea AVI de prueba (video MJPG + audio PCM16) sin ffmpeg, y un archivo .expect con lo que contiene.
Cada fotograma i lleva el numero i codificado en bloques blanco/negro (esquina superior izquierda) sobre un degradado que
cambia con i. El audio es un tono (frecuencia distinta por segundo) para poder comprobar byte a byte lo reproducido.
Uso: mkavi.py SALIDA.avi [--w 320 --h 240 --fps 25 --frames 50 --rate 44100 --channels 2 --no-audio --rec --junk
                          --audio-first --q 80 --sub 0|1|2]"""
import argparse, io, math, struct
from PIL import Image

def frame_image(i, w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255 // max(w - 1, 1) + i * 7) % 256, (y * 255 // max(h - 1, 1) + i * 3) % 256, (i * 11) % 256)
    bs = max(w // 32, 2)
    for b in range(16):
        v = 255 if (i >> b) & 1 else 0
        for yy in range(bs):
            for xx in range(bs):
                if b * bs + xx < w and yy < h: px[b * bs + xx, yy] = (v, v, v)
    return im

def audio_block(start, n, rate, ch):
    out = bytearray()
    for k in range(start, start + n):
        t = k / rate
        f = 440 + 110 * int(t)
        v = int(9000 * math.sin(2 * math.pi * f * t)) + ((((k * 1103515245 + 12345) >> 8) & 255) - 128)   # tono + ruido determinista: nunca es periodico
        for c in range(ch): out += struct.pack("<h", v if c == 0 else -v // 2)
    return bytes(out)

def chunk(fcc, data):
    d = fcc + struct.pack("<I", len(data)) + data
    return d + (b"\0" if len(data) & 1 else b"")

def lst(kind, payload):
    return b"LIST" + struct.pack("<I", len(payload) + 4) + kind + payload

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out"); ap.add_argument("--w", type=int, default=320); ap.add_argument("--h", type=int, default=240)
    ap.add_argument("--fps", type=int, default=25); ap.add_argument("--frames", type=int, default=50)
    ap.add_argument("--rate", type=int, default=44100); ap.add_argument("--channels", type=int, default=2)
    ap.add_argument("--no-audio", action="store_true"); ap.add_argument("--rec", action="store_true")
    ap.add_argument("--junk", action="store_true"); ap.add_argument("--audio-first", action="store_true")
    ap.add_argument("--q", type=int, default=80); ap.add_argument("--sub", type=int, default=2)
    a = ap.parse_args()
    audio = not a.no_audio
    ba = a.channels * 2
    vstream, astream = 0, 1
    frames, achunks = [], []
    apos = 0
    for i in range(a.frames):
        b = io.BytesIO(); frame_image(i, a.w, a.h).save(b, "JPEG", quality=a.q, subsampling=a.sub)
        frames.append(b.getvalue())
        if audio:
            n = (i + 1) * a.rate // a.fps - apos
            achunks.append(audio_block(apos, n, a.rate, a.channels)); apos += n
    # cabeceras
    usec = 1000000 // a.fps
    avih = struct.pack("<IIIIIIIIII4I", usec, 0, 0, 0x10, a.frames, 0, 2 if audio else 1, 1 << 20, a.w, a.h, 0, 0, 0, 0)
    strh_v = struct.pack("<4s4sIHHIIIIIIII4h", b"vids", b"MJPG", 0, 0, 0, 0, 1, a.fps, 0, a.frames, 1 << 20, 0xFFFFFFFF, 0, 0, 0, a.w, a.h)
    strf_v = struct.pack("<IiiHHI5I", 40, a.w, a.h, 1, 24, 0x47504A4D, a.w * a.h * 3, 0, 0, 0, 0)
    strl_v = lst(b"strl", chunk(b"strh", strh_v) + chunk(b"strf", strf_v))
    hdrl = chunk(b"avih", avih) + strl_v
    if audio:
        strh_a = struct.pack("<4s4sIHHIIIIIIII4h", b"auds", b"\0\0\0\0", 0, 0, 0, 0, ba, a.rate * ba, 0, apos, 1 << 16, 0xFFFFFFFF, ba, 0, 0, 0, 0)
        strf_a = struct.pack("<HHIIHH", 1, a.channels, a.rate, a.rate * ba, ba, 16)
        hdrl += lst(b"strl", chunk(b"strh", strh_a) + chunk(b"strf", strf_a))
    movi = b""
    idx = b""
    expect_audio = 0
    for i in range(a.frames):
        parts = []
        vfcc = b"%02ddc" % vstream
        parts.append((vfcc, frames[i]))
        if audio:
            ap_ = (b"%02dwb" % astream, achunks[i]); expect_audio += len(achunks[i])
            parts = [ap_] + parts if a.audio_first else parts + [ap_]
        body = b""
        for (f, d) in parts:
            off = len(movi) + (len(body) if not a.rec else 0)
            body += chunk(f, d)
        if a.rec: movi += lst(b"rec ", body)
        else: movi += body
    # idx1 (ignorado por el lector, pero asi son los AVI reales)
    pos = 4
    for i in range(a.frames):
        idx += struct.pack("<4sIII", b"00dc", 0x10, pos, len(frames[i])); pos += 8 + len(frames[i]) + (len(frames[i]) & 1)
    body = b"hdrl" + hdrl[0:0]
    riff = b"AVI " + lst(b"hdrl", hdrl)
    if a.junk: riff += chunk(b"JUNK", b"\0" * 777)
    riff += lst(b"movi", movi) + chunk(b"idx1", idx)
    open(a.out, "wb").write(b"RIFF" + struct.pack("<I", len(riff)) + riff)
    vbytes = sum(len(f) for f in frames)
    with open(a.out + ".expect", "w") as f:
        f.write(f"width {a.w}\nheight {a.h}\nusec {usec}\nframes {a.frames}\naudio {1 if audio else 0}\nrate {a.rate}\nchannels {a.channels}\n"
                f"abytes {expect_audio}\nvbytes {vbytes}\n")
    for i, fr in enumerate(frames):
        if i in (0, a.frames - 1): open(f"{a.out}.f{i}.jpg", "wb").write(fr)
    if audio:
        open(a.out + ".pcm", "wb").write(b"".join(achunks))

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Traduce las direcciones de un panic/aviso del vigilante de SOMA a nombres de funcion.

Uso:  python3 tools/symbolize.py LOG [ELF]        (LOG '-' = entrada estandar)
      ELF por defecto: build/x86_64/soma.elf (o build/aarch64/soma.elf si no existe)

Lee la tabla de simbolos del ELF directamente (sin nm/addr2line: en macOS el nm de
Xcode no lee ELF). Anota las lineas de BACKTRACE (#N, LR) y las de RIP/PC/ELR con
<funcion+0xOFFSET>. El resto del log se copia tal cual.
"""
import bisect
import re
import struct
import sys
from pathlib import Path

ADDR = re.compile(r"0x[0-9a-fA-F]{8,16}")
LINE = re.compile(r"^\s*(#\d+|LR|PC)\b|RIP:|ELR_EL1")


def load_symbols(path):
    d = Path(path).read_bytes()
    if d[:4] != b"\x7fELF" or d[4] != 2:
        raise SystemExit(f"{path}: no es un ELF de 64 bits")
    le = "<" if d[5] == 1 else ">"
    shoff, = struct.unpack_from(le + "Q", d, 0x28)
    shentsize, shnum = struct.unpack_from(le + "HH", d, 0x3A)
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from(le + "IIQQQQIIQQ", d, o)
        secs.append((typ, off, size, link, entsize))
    syms = []
    for typ, off, size, link, entsize in secs:
        if typ != 2:                               # SHT_SYMTAB
            continue
        _, stroff, _, _, _ = secs[link]
        for o in range(off, off + size, entsize or 24):
            nm, info, other, shndx, value, sz = struct.unpack_from(le + "IBBHQQ", d, o)
            kind = info & 0xF
            if kind not in (0, 2) or value == 0 or shndx == 0:   # NOTYPE (etiquetas asm) y FUNC
                continue
            end = d.index(b"\0", stroff + nm)
            name = d[stroff + nm:end].decode(errors="replace")
            if name and not name.startswith("$"):
                syms.append((value, sz, name))
    syms.sort()
    return syms


def resolve(syms, keys, addr):
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        return None
    value, size, name = syms[i]
    off = addr - value
    if size and off >= size and off > 0x1000:      # claramente fuera de la funcion
        return None
    return f"{name}+0x{off:X}"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    log = sys.stdin.read() if sys.argv[1] == "-" else Path(sys.argv[1]).read_text(errors="replace")
    elf = sys.argv[2] if len(sys.argv) > 2 else next(
        (p for p in ("build/x86_64/soma.elf", "build/aarch64/soma.elf") if Path(p).is_file()), None)
    if not elf:
        print("ERROR: indica el ELF (build/<arq>/soma.elf)", file=sys.stderr)
        return 2
    syms = load_symbols(elf)
    keys = [s[0] for s in syms]
    for line in log.splitlines():
        if LINE.search(line):
            def sub(m):
                r = resolve(syms, keys, int(m.group(0), 16))
                return f"{m.group(0)} <{r}>" if r else m.group(0)
            line = ADDR.sub(sub, line, count=1)
        print(line)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

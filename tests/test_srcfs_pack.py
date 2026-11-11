#!/usr/bin/env python3
"""Pruebas del empaquetador SRCFS REAL (ejecutar.py::pack_sources) y generador de la imagen de la
prueba cruzada con el lector del kernel (tests/test_srcfs.c).

  python3 tests/test_srcfs_pack.py                    # pruebas unitarias del empaquetador
  python3 tests/test_srcfs_pack.py --emit IMG MANIFEST  # empaqueta src/ real en IMG (16 MiB) y escribe
                                                      # MANIFEST con 'nombre bytes' por archivo
La funcion se extrae de ejecutar.py y se ejecuta tal cual (no es una copia).
"""
import contextlib
import io
import re
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECTOR = 512
DISK = 16 * 1024 * 1024
fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print(f"  FALLO: {msg}")


def load_packer(src_dir: Path, hdd: Path):
    text = (ROOT / "ejecutar.py").read_text(encoding="utf-8")
    a = text.index("SRCFS_LBA = ")
    b = text.index("def pack_sources")
    end = b + len("def pack_sources")
    m = re.search(r"\n(?=\S)", text[end:])
    ns = dict(Path=Path, SECTOR=SECTOR, SRC_DIR=src_dir, ALLOWED_EXT={".c", ".h", ".s"}, HDD=hdd)
    exec(text[a:end + m.start()], ns)
    return ns


def pack(src_dir: Path):
    hdd = src_dir.parent / "hdd.img"
    hdd.write_bytes(b"\0" * DISK)
    ns = load_packer(src_dir, hdd)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ns["pack_sources"]()
    return ns, hdd, buf.getvalue()


def read_image(hdd: Path, ns):
    """Lee la imagen como lo hace el kernel (srcfs.c)."""
    d = hdd.read_bytes()
    base = ns["SRCFS_LBA"] * SECTOR
    idx = d[base:base + ns["SRCFS_INDEX_SECTORS"] * SECTOR]
    if idx[:8] != b"SRCFSv01":
        return None
    count = struct.unpack_from("<I", idx, 8)[0]
    cap = ns["SRCFS_INDEX_SECTORS"] * SECTOR // 64 - 1
    out = {}
    for i in range(1, min(count, cap) + 1):
        e = idx[i * 64:(i + 1) * 64]
        name = e[:48].split(b"\0")[0].decode()
        lba, size = struct.unpack_from("<II", e, 48)
        out[name] = (lba, size, d[lba * SECTOR:lba * SECTOR + size])
    return out


def tree(base: Path, files: dict):
    for rel, data in files.items():
        p = base / "src" / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return base / "src"


def test_muchos_archivos():
    with tempfile.TemporaryDirectory() as td:
        files = {f"d{i % 7}/f{i:03d}.c": (b"int x%d;\n" % i) * (1 + i * 5) for i in range(100)}
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        check(img is not None and len(img) == 100, f"deberian empaquetarse 100 archivos (hay {len(img or {})})")
        check("AVISO" not in log, "sin avisos con 100 archivos")
        bad = [n for n, (_, _, data) in (img or {}).items() if data != files[n]]
        check(not bad, f"contenido distinto en {bad[:3]}")
        first = min(l for l, _, _ in img.values())
        check(first == ns["SRCFS_LBA"] + ns["SRCFS_INDEX_SECTORS"], "los datos empiezan justo tras el indice")


def test_tope_de_entradas():
    with tempfile.TemporaryDirectory() as td:
        files = {f"m/f{i:04d}.h": b"x\n" for i in range(300)}
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        cap = ns["SRCFS_INDEX_SECTORS"] * SECTOR // 64 - 1
        check(cap == 255, f"capacidad del indice = 255 (es {cap})")
        check(len(img) == cap, f"se empaquetan exactamente {cap} (hay {len(img)})")
        check("solo caben" in log, "avisa de que no caben todos")


def test_tablas_de_datos_se_omiten_primero():
    with tempfile.TemporaryDirectory() as td:
        files = {"fonts/glifos.h": b"A" * (3 * 1024 * 1024)}
        for i in range(10):
            files[f"core/c{i}.c"] = b"B" * (300 * 1024)             # 3 MiB de codigo
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        check("fonts/glifos.h" not in img, "la tabla de datos se omite si no cabe todo")
        check(all(f"core/c{i}.c" in img for i in range(10)), "todo el codigo se conserva")
        check("se omite fonts/glifos.h" in log, "lo avisa")


def test_nunca_fuera_de_la_region():
    with tempfile.TemporaryDirectory() as td:
        files = {f"core/g{i}.c": b"C" * (1024 * 1024) for i in range(6)}      # 6 MiB de codigo
        ns, hdd, log = pack(tree(Path(td), files))
        img = read_image(hdd, ns)
        limit = ns["SRCFS_LIMIT_LBA"]
        check(all(l + (s + SECTOR - 1) // SECTOR <= limit for l, s, _ in img.values()),
              "ningun archivo termina mas alla de SRCFS_LIMIT_LBA")
        raw = hdd.read_bytes()
        check(raw[limit * SECTOR:] == b"\0" * (DISK - limit * SECTOR), "nada escrito tras la region SRCFS")
        check(0 < len(img) < 6, f"se empaqueta lo que cabe (hay {len(img)} de 6)")
        check("sin espacio" in log, "lo avisa")


def test_fuentes_reales():
    src = ROOT / "src"
    if not src.is_dir():
        return
    ns, hdd, log = pack(src)
    img = read_image(hdd, ns)
    reales = [p for p in src.rglob("*") if p.is_file() and p.suffix.lower() in {".c", ".h", ".s"}]
    check(img is not None and len(img) >= min(len(reales), 255) - 5,
          f"las fuentes reales caben casi todas ({len(img or {})} de {len(reales)})")
    check("src/core/mem.c".replace("src/", "") in img, "core/mem.c esta empaquetado")
    bad = [n for n, (_, _, data) in img.items() if data != (src / n).read_bytes()]
    check(not bad, f"contenido distinto en {bad[:3]}")


def emit(img_path: Path, manifest: Path):
    src = ROOT / "src"
    ns, hdd, log = pack(src)
    img_path.parent.mkdir(parents=True, exist_ok=True)
    img_path.write_bytes(hdd.read_bytes())
    img = read_image(hdd, ns)
    manifest.write_text("".join(f"{n} {s}\n" for n, (_, s, _) in sorted(img.items())))
    print(f"[srcfs] imagen de prueba: {len(img)} fuentes -> {img_path}")


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--emit":
        emit(Path(sys.argv[2]), Path(sys.argv[3]))
        return 0
    for t in (test_muchos_archivos, test_tope_de_entradas, test_tablas_de_datos_se_omiten_primero,
              test_nunca_fuera_de_la_region, test_fuentes_reales):
        t()
    print(f"[srcfs-pack] {checks} comprobaciones, {fails} fallos")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())

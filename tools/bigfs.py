#!/usr/bin/env python3
"""SOMA_PARCHE_018_BIGFS: herramienta del host para el almacen de archivos grandes (formato SOMABIG1).

Meter audio, imagenes, video o programas en el disco de SOMA desde tu Mac, sin arrancar el SO:

  python3 tools/bigfs.py hdd.img --offset-lba 16384 info
  python3 tools/bigfs.py hdd.img --offset-lba 16384 format              # SOLO si no esta formateado
  python3 tools/bigfs.py hdd.img --offset-lba 16384 mkdir /musica
  python3 tools/bigfs.py hdd.img --offset-lba 16384 put ~/cancion.wav /musica/cancion.wav
  python3 tools/bigfs.py hdd.img --offset-lba 16384 ls /musica
  python3 tools/bigfs.py hdd.img --offset-lba 16384 get /musica/cancion.wav copia.wav
  python3 tools/bigfs.py hdd.img --offset-lba 16384 fsck

--offset-lba es donde empieza la region del disco de SOMA (16384, ver src/fs/disk_layout.h). Para una imagen
suelta de pruebas, omitelo (0) y crea el archivo con:  format --create-mb 16

Implementa EXACTAMENTE el formato de src/fs/bigfs.c (se comprueba en make test con una prueba cruzada en ambos
sentidos). No uses esta herramienta mientras QEMU tiene el disco abierto.
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path

SECTOR = 512
CLUSTER_SECT = 8
CLUSTER = SECTOR * CLUSTER_SECT
INODE_SIZE = 256
NAME_MAX = 63
MAX_EXTENTS = 20
T_FREE, T_FILE, T_DIR = 0, 1, 2
MAGIC = b"SOMABIG1"
VERSION = 1
INODE_FMT = "<IIIIQQ64s" + "II" * MAX_EXTENTS          # 4*4 + 8*2 + 64 + 160 = 256
assert struct.calcsize(INODE_FMT) == INODE_SIZE


class BigFSError(Exception):
    pass


def crc32(b: bytes) -> int:
    return zlib.crc32(b) & 0xFFFFFFFF


def fnv64(data: bytes, h: int = 1469598103934665603) -> int:
    for b in data:
        h ^= b
        h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


class Inode:
    __slots__ = ("state", "parent", "nextents", "size", "mtime", "name", "ext")

    def __init__(self):
        self.state = T_FREE; self.parent = 0; self.nextents = 0; self.size = 0; self.mtime = 0
        self.name = ""; self.ext: list[tuple[int, int]] = []

    def pack(self) -> bytes:
        ext = list(self.ext) + [(0, 0)] * (MAX_EXTENTS - len(self.ext))
        flat = [x for e in ext for x in e]
        body = struct.pack("<IIQQ64s" + "II" * MAX_EXTENTS, self.parent, len(self.ext), self.size, self.mtime,
                           self.name.encode().ljust(64, b"\0"), *flat)
        head_crc = crc32(body)
        return struct.pack("<II", self.state, head_crc) + body

    @staticmethod
    def unpack(raw: bytes):
        """Devuelve (Inode, ok_crc). Un inodo libre es todo ceros."""
        st, crc = struct.unpack_from("<II", raw, 0)
        ino = Inode(); ino.state = st
        if st == T_FREE:
            return ino, True
        vals = struct.unpack_from("<IIQQ64s" + "II" * MAX_EXTENTS, raw, 8)
        ino.parent, ino.nextents, ino.size, ino.mtime = vals[0], vals[1], vals[2], vals[3]
        nm = vals[4]
        ino.name = nm.split(b"\0", 1)[0].decode("utf-8", "replace")
        flat = vals[5:]
        ino.ext = [(flat[2 * i], flat[2 * i + 1]) for i in range(min(ino.nextents, MAX_EXTENTS))]
        return ino, crc == crc32(raw[8:])


class BigFS:
    def __init__(self, path: Path, offset_lba: int = 0, sectors: int | None = None, create_mb: int = 0):
        self.path = Path(path)
        self.base = offset_lba * SECTOR
        if create_mb and not self.path.exists():
            with open(self.path, "wb") as f:
                f.truncate(create_mb * 1024 * 1024)
        if not self.path.exists():
            raise BigFSError(f"no existe {self.path}")
        size = self.path.stat().st_size
        self.total = (size - self.base) // SECTOR if sectors is None else sectors
        if self.total < 16:
            raise BigFSError("la region es demasiado pequena")
        self.f = open(self.path, "r+b")
        self.ino: list[Inode] = []
        self.bitmap: bytearray | None = None
        self.sb = None
        self.mounted = False
        self.clock = 0

    # ---- E/S
    def rd(self, lba: int, n: int = 1) -> bytes:
        if lba + n > self.total:
            raise BigFSError("lectura fuera de la region")
        self.f.seek(self.base + lba * SECTOR)
        return self.f.read(n * SECTOR)

    def wr(self, lba: int, data: bytes) -> None:
        if lba + len(data) // SECTOR > self.total:
            raise BigFSError("escritura fuera de la region")
        self.f.seek(self.base + lba * SECTOR)
        self.f.write(data)

    def close(self):
        self.f.flush(); self.f.close()

    # ---- formato
    @staticmethod
    def _layout(total: int, inodes: int):
        isect = (inodes + 1) // 2
        ds = 1 + isect
        ds = (ds + CLUSTER_SECT - 1) // CLUSTER_SECT * CLUSTER_SECT
        if total <= ds + CLUSTER_SECT * 2:
            raise BigFSError("region demasiado pequena")
        clusters = min((total - ds) // CLUSTER_SECT, 0xFFFFFFF0)
        return ds, clusters

    def _super_bytes(self, inodes: int, ds: int, clusters: int) -> bytes:
        head = struct.pack("<8sIIQIIII", MAGIC, VERSION, CLUSTER_SECT, self.total, inodes, 1, ds, clusters)
        return (head + struct.pack("<I", crc32(head))).ljust(SECTOR, b"\0")

    def format(self, inodes: int = 0):
        if inodes == 0:
            c = self.total // CLUSTER_SECT // 8
            inodes = 256 if c < 256 else 4096 if c > 4096 else c
        inodes = (inodes + 1) & ~1
        ds, clusters = self._layout(self.total, inodes)
        self.wr(0, b"\0" * SECTOR)                      # invalida primero (como el C)
        root = Inode(); root.state = T_DIR
        table = bytearray(b"\0" * ((ds - 1) * SECTOR))
        table[0:INODE_SIZE] = root.pack()
        self.wr(1, bytes(table))
        self.wr(0, self._super_bytes(inodes, ds, clusters))   # el superbloque, el ultimo
        self.f.flush()
        self.mount()

    # ---- montaje
    def mount(self):
        raw = self.rd(0)
        magic, ver, cs, total, inodes, istart, ds, clusters = struct.unpack_from("<8sIIQIIII", raw, 0)
        (crc,) = struct.unpack_from("<I", raw, 40)
        if magic != MAGIC:
            raise BigFSError("no hay un SOMABIG1 en la region (usa 'format')")
        if crc != crc32(raw[:40]) or ver != VERSION or cs != CLUSTER_SECT or total > self.total \
           or inodes < 2 or inodes % 2 or istart != 1:
            raise BigFSError("superbloque danado")
        ds2, cl2 = self._layout(total, inodes)
        if ds2 != ds or cl2 != clusters:
            raise BigFSError("superbloque inconsistente")
        self.sb = dict(inodes=inodes, data_start=ds, clusters=clusters)
        tab = self.rd(1, (inodes + 1) // 2)
        self.ino = []
        self.bad_crc = 0
        for i in range(inodes):
            ino, ok = Inode.unpack(tab[i * INODE_SIZE:(i + 1) * INODE_SIZE])
            if not ok:
                self.bad_crc += 1
                ino = Inode(); ino.state = -1          # marca: danado
            self.ino.append(ino)
        self.bitmap = bytearray((clusters + 7) // 8)
        for i, ino in enumerate(self.ino):
            if ino.state in (T_FILE, T_DIR) and i != 0:
                for s, c in ino.ext:
                    for k in range(c):
                        self._bit_set(s + k)
        self.mounted = True

    def _bit_get(self, i): return (self.bitmap[i >> 3] >> (i & 7)) & 1
    def _bit_set(self, i): self.bitmap[i >> 3] |= 1 << (i & 7)
    def _bit_clr(self, i): self.bitmap[i >> 3] &= ~(1 << (i & 7)) & 0xFF

    def free_clusters(self) -> int:
        return sum(1 for i in range(self.sb["clusters"]) if not self._bit_get(i))

    # ---- inodos y rutas
    def _commit(self, i: int):
        self.clock += 1
        ino = self.ino[i]
        sec = i // 2
        a, b = self.ino[sec * 2], self.ino[sec * 2 + 1]
        self.wr(1 + sec, (a.pack() + b.pack()) if True else b"")

    def _parts(self, path: str):
        parts = []
        for p in path.split("/"):
            if p in ("", "."):
                continue
            parts.append(p)
        return parts

    def _child(self, d: int, name: str):
        for i, ino in enumerate(self.ino):
            if i and ino.state in (T_FILE, T_DIR) and ino.parent == d and ino.name == name:
                return i
        return None

    def lookup(self, path: str):
        cur = 0
        for p in self._parts(path):
            if p == "..":
                cur = self.ino[cur].parent
                continue
            if self.ino[cur].state != T_DIR:
                raise BigFSError("no es un directorio: " + path)
            c = self._child(cur, p)
            if c is None:
                raise BigFSError("no existe: " + path)
            cur = c
        return cur

    def _split(self, path: str):
        parts = self._parts(path)
        if not parts:
            raise BigFSError("ruta invalida")
        name = parts[-1]
        if name in ("..",) or not (1 <= len(name.encode()) <= NAME_MAX) or any(ord(ch) < 32 or ch == "\x7f" for ch in name):
            raise BigFSError("nombre invalido: " + name)
        parent = self.lookup("/".join(parts[:-1]))
        if self.ino[parent].state != T_DIR:
            raise BigFSError("no es un directorio")
        return parent, name

    def _new_node(self, path: str, state: int) -> int:
        parent, name = self._split(path)
        if self._child(parent, name) is not None:
            raise BigFSError("ya existe: " + path)
        for i in range(1, len(self.ino)):
            if self.ino[i].state == T_FREE:
                ino = self.ino[i]
                ino.state = state; ino.parent = parent; ino.name = name; ino.size = 0; ino.ext = []; ino.nextents = 0
                self._commit(i)
                return i
        raise BigFSError("sin inodos libres")

    def mkdir(self, path: str):
        if path.strip("/") == "":
            return
        cur = ""
        for p in self._parts(path):                      # mkdir -p
            cur += "/" + p
            try:
                i = self.lookup(cur)
                if self.ino[i].state != T_DIR:
                    raise BigFSError("existe y no es un directorio: " + cur)
            except BigFSError as e:
                if "no existe" not in str(e):
                    raise
                self._new_node(cur, T_DIR)

    # ---- datos
    def _alloc(self, ino: Inode, need: int):
        """Reserva `need` clusteres (extiende el ultimo extent si puede; si no, primer hueco que quepa o el mayor)."""
        if need == 0:
            return
        total = self.sb["clusters"]
        remaining = need
        if ino.ext:
            s, c = ino.ext[-1]
            pos = s + c
            while remaining and pos < total and not self._bit_get(pos):
                self._bit_set(pos); c += 1; pos += 1; remaining -= 1
            ino.ext[-1] = (s, c)
        while remaining:
            best_s = best_n = 0
            c = 0
            found = False
            while c < total:
                if self._bit_get(c):
                    c += 1
                    continue
                run = c
                while run < total and not self._bit_get(run) and run - c < remaining:
                    run += 1
                n = run - c
                if n >= remaining:
                    best_s, best_n, found = c, n, True
                    break
                if n > best_n:
                    best_s, best_n = c, n
                c = run
            if best_n == 0 or len(ino.ext) >= MAX_EXTENTS:
                raise BigFSError("sin espacio o demasiado fragmentado")
            take = min(best_n, remaining)
            for k in range(take):
                self._bit_set(best_s + k)
            ino.ext.append((best_s, take))
            remaining -= take

    def _lba_of(self, ino: Inode, off: int):
        cidx, within = divmod(off, CLUSTER)
        cum = 0
        for s, c in ino.ext:
            if cidx < cum + c:
                return self.sb["data_start"] + (s + cidx - cum) * CLUSTER_SECT + within // SECTOR, within % SECTOR, (cum + c - cidx) * CLUSTER - within
            cum += c
        raise BigFSError("offset fuera de los extents")

    def write_file(self, path: str, data: bytes):
        """Crea (o sustituye) un archivo con `data`. Escribe los datos y confirma el inodo al final."""
        try:
            i = self.lookup(path)
            if self.ino[i].state != T_FILE:
                raise BigFSError("existe y no es un archivo: " + path)
            self.remove(path)
        except BigFSError as e:
            if "no existe" not in str(e):
                raise
        parent_dir = "/".join(self._parts(path)[:-1])
        if parent_dir:
            self.mkdir(parent_dir)
        i = self._new_node(path, T_FILE)
        ino = self.ino[i]
        need = (len(data) + CLUSTER - 1) // CLUSTER
        try:
            self._alloc(ino, need)
        except BigFSError:
            for s, c in ino.ext:
                for k in range(c):
                    self._bit_clr(s + k)
            ino.state = T_FREE; ino.ext = []; ino.name = ""
            self._commit(i)
            raise
        off = 0
        while off < len(data):
            lba, so, run = self._lba_of(ino, off)
            n = min(len(data) - off, run)
            chunk = data[off:off + n]
            if so or n % SECTOR:                          # extremos no alineados: lectura-modificacion-escritura
                head = so
                total = head + n
                nsec = (total + SECTOR - 1) // SECTOR
                buf = bytearray(self.rd(lba, nsec))
                buf[head:head + n] = chunk
                self.wr(lba, bytes(buf))
            else:
                self.wr(lba, chunk)
            off += n
        ino.size = len(data)
        self._commit(i)                                   # confirmacion atomica al final

    def read_file(self, path: str) -> bytes:
        i = self.lookup(path)
        ino = self.ino[i]
        if ino.state != T_FILE:
            raise BigFSError("no es un archivo: " + path)
        out = bytearray()
        off = 0
        while off < ino.size:
            lba, so, run = self._lba_of(ino, off)
            n = min(ino.size - off, run)
            nsec = (so + n + SECTOR - 1) // SECTOR
            buf = self.rd(lba, nsec)
            out += buf[so:so + n]
            off += n
        return bytes(out)

    def remove(self, path: str):
        i = self.lookup(path)
        if i == 0:
            raise BigFSError("no se puede borrar la raiz")
        ino = self.ino[i]
        if ino.state == T_DIR and any(j and x.state in (T_FILE, T_DIR) and x.parent == i for j, x in enumerate(self.ino)):
            raise BigFSError("el directorio no esta vacio")
        for s, c in ino.ext:
            for k in range(c):
                self._bit_clr(s + k)
        self.ino[i] = Inode()
        sec = i // 2
        self.wr(1 + sec, self.ino[sec * 2].pack() + self.ino[sec * 2 + 1].pack())

    def listdir(self, path: str):
        d = self.lookup(path)
        if self.ino[d].state != T_DIR:
            raise BigFSError("no es un directorio")
        return [(i, x) for i, x in enumerate(self.ino) if i and x.state in (T_FILE, T_DIR) and x.parent == d]

    # ---- fsck (lee la tabla cruda del disco)
    def fsck(self) -> dict:
        raw = self.rd(0)
        rep = dict(problems=0, bad_crc=0, bad_extent=0, overlap=0, orphan=0, bad_dir=0, bad_name=0, dup_name=0, bad_super=0)
        try:
            self.mount()
        except BigFSError:
            rep["bad_super"] = 1; rep["problems"] = 1
            return rep
        n = self.sb["inodes"]
        tab = self.rd(1, (n + 1) // 2)
        inodes = []
        verdict = [0] * n
        for i in range(n):
            ino, ok = Inode.unpack(tab[i * INODE_SIZE:(i + 1) * INODE_SIZE])
            inodes.append(ino)
            if ino.state == T_FREE:
                continue
            verdict[i] = 1
            if ino.state not in (T_FILE, T_DIR) or not ok:
                rep["bad_crc"] += 1; verdict[i] = 2; continue
            if i == 0:
                if ino.state != T_DIR or ino.name or ino.parent != 0:
                    rep["bad_dir"] += 1; verdict[i] = 2
                continue
            nm = ino.name.encode()
            if not (1 <= len(nm) <= NAME_MAX) or any(b < 32 or b == 47 for b in nm):
                rep["bad_name"] += 1; verdict[i] = 2; continue
            if ino.nextents > MAX_EXTENTS:
                rep["bad_extent"] += 1; verdict[i] = 2; continue
            if ino.state == T_DIR and (ino.nextents or ino.size):
                rep["bad_dir"] += 1; verdict[i] = 2; continue
            cl = 0
            for s, c in ino.ext:
                if c == 0 or s + c > self.sb["clusters"]:
                    rep["bad_extent"] += 1; verdict[i] = 2; break
                cl += c
            if verdict[i] == 2:
                continue
            if ino.state == T_FILE and ino.size > cl * CLUSTER:
                rep["bad_extent"] += 1; verdict[i] = 2
        if inodes[0].state != T_DIR:
            rep["bad_dir"] += 1; verdict[0] = 2
        for i in range(1, n):
            if verdict[i] != 1:
                continue
            cur, steps, good = i, 0, True
            while cur != 0:
                p = inodes[cur].parent
                if p >= n or verdict[p] != 1 or inodes[p].state != T_DIR:
                    good = False; break
                cur = p; steps += 1
                if steps > n:
                    good = False; break
            if not good:
                rep["orphan"] += 1; verdict[i] = 2
        for i in range(1, n):
            if verdict[i] != 1:
                continue
            for j in range(i + 1, n):
                if verdict[j] == 1 and inodes[j].parent == inodes[i].parent and inodes[j].name == inodes[i].name:
                    rep["dup_name"] += 1; verdict[j] = 2
        used = set()
        for i in range(n):
            if verdict[i] != 1:
                continue
            cl = {s + k for s, c in inodes[i].ext for k in range(c)}
            if cl & used:
                rep["overlap"] += 1; verdict[i] = 2; continue
            used |= cl
        rep["problems"] = sum(v for k, v in rep.items() if k not in ("problems", "bad_super"))
        return rep

    def info(self) -> dict:
        used_inodes = sum(1 for i, x in enumerate(self.ino) if i and x.state in (T_FILE, T_DIR))
        return dict(total_clusters=self.sb["clusters"], free_clusters=self.free_clusters(),
                    inodes=self.sb["inodes"], used_inodes=used_inodes + 1, cluster_bytes=CLUSTER)


def _emit(fs: BigFS, manifest: Path, spec):
    """Para la prueba cruzada: crea archivos con contenido determinista y escribe un manifiesto."""
    lines = []
    for p in sorted({str(Path(s[0]).parent) for s in spec if str(Path(s[0]).parent) != "/"}):
        fs.mkdir(p)
        lines.append(f"D {p} 0 0")
    for path, n, seed in spec:
        data = bytes(((i * 2654435761 + seed * 40503) >> 13) & 0xFF for i in range(n))
        fs.write_file(path, data)
        lines.append(f"F {path} {n} {fnv64(data):x}")
    manifest.write_text("\n".join(lines) + "\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Herramienta del host para BIGFS (formato SOMABIG1)")
    ap.add_argument("image", type=Path)
    ap.add_argument("--offset-lba", type=int, default=0, help="inicio de la region en el disco (hdd.img: 16384)")
    ap.add_argument("--sectors", type=int, default=None, help="tamano de la region en sectores (por defecto, hasta el final)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    f = sub.add_parser("format"); f.add_argument("--inodes", type=int, default=0); f.add_argument("--create-mb", type=int, default=0)
    f.add_argument("--yes", action="store_true", help="confirma que quieres BORRAR el sistema de archivos existente")
    sub.add_parser("info"); sub.add_parser("fsck")
    s = sub.add_parser("ls"); s.add_argument("path", nargs="?", default="/")
    s = sub.add_parser("mkdir"); s.add_argument("path")
    s = sub.add_parser("put"); s.add_argument("local", type=Path); s.add_argument("path")
    s = sub.add_parser("get"); s.add_argument("path"); s.add_argument("local", type=Path)
    s = sub.add_parser("rm"); s.add_argument("path")
    s = sub.add_parser("verify"); s.add_argument("manifest", type=Path)
    s = sub.add_parser("emit"); s.add_argument("manifest", type=Path)
    a = ap.parse_args()
    try:
        fs = BigFS(a.image, a.offset_lba, a.sectors, getattr(a, "create_mb", 0))
        if a.cmd == "format":
            try:
                fs.mount()
                if not a.yes:
                    print("ERROR: la region ya tiene un SOMABIG1; usa --yes si quieres borrarlo.", file=sys.stderr)
                    return 2
            except BigFSError:
                pass
            fs.format(a.inodes)
            print(f"formateado: {fs.info()}")
            return 0
        fs.mount()
        if a.cmd == "info":
            print(fs.info())
        elif a.cmd == "fsck":
            rep = fs.fsck()
            print(rep)
            return 1 if rep["problems"] else 0
        elif a.cmd == "ls":
            for i, x in sorted(fs.listdir(a.path), key=lambda t: t[1].name):
                print(f"{'d' if x.state == T_DIR else '-'} {x.size:>12} {x.name}")
        elif a.cmd == "mkdir":
            fs.mkdir(a.path)
        elif a.cmd == "put":
            fs.write_file(a.path, a.local.read_bytes())
            print(f"{a.local} -> {a.path} ({a.local.stat().st_size} bytes)")
        elif a.cmd == "get":
            a.local.write_bytes(fs.read_file(a.path))
        elif a.cmd == "rm":
            fs.remove(a.path)
        elif a.cmd == "verify":
            bad = 0
            for line in a.manifest.read_text().splitlines():
                kind, path, size, h = (line.split() + ["0", "0"])[:4]
                if kind == "D":
                    if fs.ino[fs.lookup(path)].state != T_DIR:
                        bad += 1; print("no es directorio:", path)
                    continue
                data = fs.read_file(path)
                if len(data) != int(size) or fnv64(data) != int(h, 16):
                    bad += 1; print("DISTINTO:", path)
            rep = fs.fsck()
            print(f"verify: {bad} archivos distintos, fsck problemas={rep['problems']}")
            return 1 if (bad or rep["problems"]) else 0
        elif a.cmd == "emit":
            spec = [("/leeme.txt", 11, 1), ("/musica/a.wav", 700000, 2), ("/musica/rock/b.wav", 1500001, 3),
                    ("/prog/hola.elf", 8920, 4), ("/vacio", 0, 5), ("/prog/grande.bin", 3000000, 6)]
            _emit(fs, a.manifest, spec)
        fs.close()
        return 0
    except BigFSError as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

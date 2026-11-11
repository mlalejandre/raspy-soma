#!/usr/bin/env python3
"""PARCHE 050: constructor de imagenes de disco FAT12/16/32 (con MBR o sin el), nombres largos, subdirectorios y ficheros fragmentados.
Lo usan las pruebas del host (make test) y ejecutar.py (disco USB de prueba de QEMU). Solo biblioteca estandar."""
import os, random, struct, sys, zlib

SEC = 512

def _lfn_checksum(short11: bytes) -> int:
    s = 0
    for b in short11:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
    return s

def _is_83(name: str):
    if name in (".", ".."):
        return None
    if "." in name:
        base, ext = name.rsplit(".", 1)
    else:
        base, ext = name, ""
    ok = lambda t, n: 0 < len(t) <= n and all(c.isalnum() and c.isascii() or c in "_-$~!#%&'()@^`{}" for c in t) and t == t.upper()
    if ok(base, 8) and (ext == "" or ok(ext, 3)):
        return base.encode(), ext.encode()
    return None

class Disk:
    def __init__(self, total_sectors, fat_bits=16, mbr=True, part_start=2048, spc=None, sig=b"", label="SOMADISK", shuffle=False, seed=1):
        assert fat_bits in (12, 16, 32)
        self.total, self.bits, self.mbr = total_sectors, fat_bits, mbr
        self.pstart = part_start if mbr else 0
        self.psec = total_sectors - self.pstart
        self.sig, self.label, self.shuffle = sig, label.upper().ljust(11)[:11], shuffle
        self.rng = random.Random(seed)
        self.reserved = 32 if fat_bits == 32 else 1
        self.nfats = 2
        self.root_entries = 0 if fat_bits == 32 else 512
        self.root_secs = (self.root_entries * 32 + SEC - 1) // SEC
        self.spc = spc or (1 if fat_bits != 16 else 2)
        # tamano de la FAT por iteracion
        fatsz = 1
        for _ in range(20):
            data = self.psec - self.reserved - self.root_secs - self.nfats * fatsz
            clusters = data // self.spc
            entries = clusters + 2
            need = (entries * (3 if fat_bits == 12 else fat_bits // 8) + (1 if fat_bits == 12 else 0))
            need = ((entries * 3 + 1) // 2 if fat_bits == 12 else entries * fat_bits // 8)
            nfs = (need + SEC - 1) // SEC
            if nfs == fatsz:
                break
            fatsz = nfs
        self.fatsz, self.clusters = fatsz, clusters
        lim = {12: (1, 4084), 16: (4085, 65524), 32: (65525, 1 << 28)}[fat_bits]
        assert lim[0] <= clusters <= lim[1], "tamano incompatible con FAT%d: %d clusters" % (fat_bits, clusters)
        self.fat = [0] * (clusters + 2)
        self.fat[0] = {12: 0xFF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[fat_bits]
        self.fat[1] = self.eoc()
        self.data = {}                       # cluster -> bytes (cluster_bytes)
        self.free = list(range(2, clusters + 2))
        if shuffle:
            self.rng.shuffle(self.free)
        self.cb = self.spc * SEC
        self.manifest = []
        self.root = {"dirs": {}, "files": {}}
        self.root_cluster = None

    def eoc(self):
        return {12: 0xFFF, 16: 0xFFFF, 32: 0x0FFFFFFF}[self.bits]

    def alloc(self, nbytes):
        n = max(1, (nbytes + self.cb - 1) // self.cb)
        assert len(self.free) >= n, "disco lleno"
        if self.shuffle:
            cl = [self.free.pop() for _ in range(n)]
        else:
            cl = sorted(self.free[:n]); self.free = self.free[n:]
        for i, c in enumerate(cl):
            self.fat[c] = cl[i + 1] if i + 1 < n else self.eoc()
        return cl

    def write_chain(self, cl, payload):
        for i, c in enumerate(cl):
            self.data[c] = payload[i * self.cb:(i + 1) * self.cb].ljust(self.cb, b"\0")

    def add_file(self, path, payload: bytes):
        parts = [p for p in path.split("/") if p]
        d = self.root
        for p in parts[:-1]:
            d = d["dirs"].setdefault(p, {"dirs": {}, "files": {}})
        d["files"][parts[-1]] = payload
        self.manifest.append(("F", "/" + "/".join(parts), len(payload), zlib.crc32(payload) & 0xFFFFFFFF))

    def _entries(self, d, parent_cluster, self_cluster, dots=True):
        out = b""
        names = set()
        def shortname(name):
            r = _is_83(name)
            if r:
                return r[0].ljust(8) + r[1].ljust(3), False
            base = "".join(c for c in name.upper().replace(" ", "") if c.isalnum() and c.isascii() or c in "_-$")[:6] or "FILE"
            ext = ""
            if "." in name:
                ext = "".join(c for c in name.rsplit(".", 1)[1].upper() if c.isalnum() and c.isascii())[:3]
            n = 1
            while True:
                cand = ("%s~%d" % (base, n)).encode().ljust(8) + ext.encode().ljust(3)
                if cand not in names:
                    names.add(cand); return cand, True
                n += 1
        def dirent(short11, attr, cluster, size, nt=0):
            return struct.pack("<11sBBBHHHHHHHI", short11, attr, nt, 0, 0x6000, 0x5821, 0x5821, cluster >> 16, 0x6000, 0x5821, cluster & 0xFFFF, size)
        def lfn(name, short11):
            u = name.encode("utf-16-le")
            chars = [u[i] | u[i + 1] << 8 for i in range(0, len(u), 2)] + [0]
            chars += [0xFFFF] * ((-len(chars)) % 13)
            cs = _lfn_checksum(short11)
            n = len(chars) // 13
            res = b""
            for k in range(n, 0, -1):
                ch = chars[(k - 1) * 13:k * 13]
                ordv = k | (0x40 if k == n else 0)
                res += struct.pack("<B5HBBB6HH2H", ordv, *ch[0:5], 0x0F, 0, cs, *ch[5:11], 0, *ch[11:13])
            return res
        if self_cluster is not None and dots:
            out += dirent(b".          ", 0x10, self_cluster, 0) + dirent(b"..         ", 0x10, parent_cluster, 0)
        subs = []
        for name, sub in sorted(d["dirs"].items()):
            cl = self.alloc(0)[0]
            sub["cluster"] = cl
            s11, need = shortname(name)
            if need or _is_83(name) is None:
                out += lfn(name, s11)
            out += dirent(s11, 0x10, cl, 0)
            subs.append((name, sub, cl))
        for name, payload in sorted(d["files"].items()):
            if payload:
                cl = self.alloc(len(payload)); self.write_chain(cl, payload); first = cl[0]
            else:
                first = 0
            s11, need = shortname(name)
            if need or _is_83(name) is None:
                out += lfn(name, s11)
            out += dirent(s11, 0x20, first, len(payload))
        return out, subs

    def _write_dir(self, d, parent_cluster, self_cluster, fixed=False, is_root=False):
        raw, subs = self._entries(d, parent_cluster, self_cluster, dots=not is_root)
        if fixed:
            assert len(raw) <= self.root_entries * 32, "raiz llena"
            self.root_raw = raw.ljust(self.root_secs * SEC, b"\0")
        else:
            if self_cluster is not None:
                need = (len(raw) + 32 + self.cb - 1) // self.cb
                chain = [self_cluster]
                while len(chain) < need:                      # ampliar el directorio
                    extra = self.alloc(0)[0]; self.fat[chain[-1]] = extra; chain.append(extra)
                self.write_chain(chain, raw)
        for name, sub, cl in subs:
            pc = 0 if (is_root or self_cluster is None) else self_cluster          # '..' de un hijo de la raiz vale 0
            self._write_dir(sub, pc, cl)

    def build(self):
        self.manifest += [("D", "/" , 0, 0)]
        if self.bits == 32:
            # raiz: reservar su cluster primero (queda en el cluster 2 si no se baraja)
            rc = self.alloc(0)[0]; self.root_cluster = rc
            self._write_dir(self.root, 0, rc, is_root=True)
            # (rc ya es la cadena de la raiz; _write_dir con self_cluster escribe . y .. que la raiz FAT32 no lleva)
        else:
            self._write_dir(self.root, 0, None, fixed=True)
        img = bytearray(self.total * SEC)
        p = self.pstart * SEC
        bs = bytearray(SEC)
        bs[0:3] = b"\xEB\x3C\x90"; bs[3:11] = b"SOMAFAT "
        struct.pack_into("<HBHBHHBHHHII", bs, 11, SEC, self.spc, self.reserved, self.nfats, self.root_entries,
                         self.psec if self.psec < 65536 and self.bits != 32 else 0, 0xF8, self.fatsz if self.bits != 32 else 0, 63, 255, self.pstart,
                         self.psec if (self.psec >= 65536 or self.bits == 32) else 0)
        if self.bits == 32:
            struct.pack_into("<IHHIHH12sBBBI11s8s", bs, 36, self.fatsz, 0, 0, self.root_cluster, 1, 6, b"", 0x80, 0, 0x29, 0x534F4D41, self.label.encode(), b"FAT32   ")
        else:
            struct.pack_into("<BBBI11s8s", bs, 36, 0x80, 0, 0x29, 0x534F4D41, self.label.encode(), b"FAT12   " if self.bits == 12 else b"FAT16   ")
        bs[510:512] = b"\x55\xAA"
        img[p:p + SEC] = bs
        if self.bits == 32:
            img[p + 6 * SEC:p + 7 * SEC] = bs
            fsi = bytearray(SEC); struct.pack_into("<I", fsi, 0, 0x41615252); struct.pack_into("<I", fsi, 484, 0x61417272)
            struct.pack_into("<II", fsi, 488, 0xFFFFFFFF, 0xFFFFFFFF); fsi[510:512] = b"\x55\xAA"
            img[p + SEC:p + 2 * SEC] = fsi
        # FAT (x2)
        if self.bits == 12:
            raw = bytearray(self.fatsz * SEC)
            for i, v in enumerate(self.fat):
                o = i * 3 // 2
                if i & 1:
                    raw[o] = (raw[o] & 0x0F) | ((v & 0xF) << 4); raw[o + 1] = v >> 4
                else:
                    raw[o] = v & 0xFF; raw[o + 1] = (raw[o + 1] & 0xF0) | (v >> 8)
        else:
            fmt = "<%d%s" % (len(self.fat), "H" if self.bits == 16 else "I")
            raw = bytearray(struct.pack(fmt, *self.fat)).ljust(self.fatsz * SEC, b"\0")
        f0 = p + self.reserved * SEC
        for k in range(self.nfats):
            img[f0 + k * self.fatsz * SEC:f0 + (k + 1) * self.fatsz * SEC] = raw
        rootpos = f0 + self.nfats * self.fatsz * SEC
        if self.bits != 32:
            img[rootpos:rootpos + self.root_secs * SEC] = self.root_raw
        datapos = rootpos + self.root_secs * SEC
        for c, blob in self.data.items():
            o = datapos + (c - 2) * self.cb
            img[o:o + self.cb] = blob
        if self.mbr:
            img[0:len(self.sig)] = self.sig
            ptype = {12: 0x01, 16: 0x0E, 32: 0x0C}[self.bits]
            struct.pack_into("<B3sB3sII", img, 446, 0x80, b"\0\2\0", ptype, b"\xFE\xFF\xFF", self.pstart, self.psec)
            img[510:512] = b"\x55\xAA"
        return bytes(img)

def sample_files(big=True):
    rng = random.Random(42)
    rnd = lambda n: bytes(rng.randrange(256) for _ in range(n))
    f = {
        "HELLO.TXT": b"Hola desde FAT\n",
        "LEEME.TXT": "Este disco USB de prueba lo ha creado tools/mkfat.py.\nTiene nombres largos, acentos (áéíóú ñ) y subcarpetas.\n".encode("utf-8"),
        "Documento largo con espacios.txt": b"nombre largo\n" * 50,
        "minusculas.txt": b"el nombre corto es MINUSC~1.TXT\n",
        "vacio.txt": b"",
        "Carpeta/Sub Carpeta/datos.bin": rnd(70000),
        "Carpeta/otro.txt": b"x" * 4096,
        "Carpeta/exacto.bin": rnd(1024),
        "niño/café.txt": "acentos en el nombre\n".encode("utf-8"),
    }
    if big:
        f["grande.bin"] = rnd(200000)
    return f

def make_disk(total, bits, mbr=True, shuffle=False, spc=None, sig=b"", big=True):
    d = Disk(total, bits, mbr=mbr, shuffle=shuffle, spc=spc, sig=sig)
    for p, b in sample_files(big).items():
        d.add_file(p, b)
    img = d.build()
    return img, d

def write_manifest(path, d):
    # directorios: numero de entradas (sin . ni ..) de cada uno
    tree = {}
    def walk(node, prefix):
        tree[prefix or "/"] = len(node["dirs"]) + len(node["files"])
        for n, s in node["dirs"].items():
            walk(s, prefix + "/" + n)
    walk(d.root, "")
    with open(path, "w", encoding="utf-8") as f:
        for kind, p, size, crc in d.manifest:
            if kind == "F":
                f.write("F\t%s\t%d\t%08x\n" % (p, size, crc))
        for p, n in sorted(tree.items()):
            f.write("D\t%s\t%d\n" % (p, n))

def make_usb_test_disk(path, size_mib=8):
    """Disco USB de prueba de QEMU: MBR con firma SOMAUSB- en el sector 0 y una particion FAT16 con unos ficheros."""
    img, _ = make_disk(size_mib * 2048, 16, mbr=True, big=False, sig=b"SOMAUSB-TEST-01\n")
    with open(path, "wb") as f:
        f.write(img)

def make_sd_test_disk(path, size_mib=64):
    """Tarjeta SD de PRUEBA de QEMU (potencia de 2): MBR con firma SOMASD-- en el sector 0 y una particion FAT16 con ficheros."""
    img, _ = make_disk(size_mib * 2048, 16, mbr=True, big=False, sig=b"SOMASD--TEST-01\n")
    with open(path, "wb") as f:
        f.write(img)

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    cases = [("fat12", 2048, 12, False, 1, False), ("fat16", 16384, 16, True, 2, True), ("fat16s", 16384, 16, True, 2, False), ("fat32", 140000, 32, True, 1, True)]
    for name, total, bits, mbr, spc, shuf in cases:
        img, d = make_disk(total, bits, mbr=mbr, shuffle=shuf, spc=spc, big=(bits != 12))
        open(os.path.join(out, name + ".img"), "wb").write(img)
        write_manifest(os.path.join(out, name + ".txt"), d)
        print(name, len(img), "bytes", "clusters", d.clusters)

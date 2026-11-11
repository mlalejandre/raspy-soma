#!/usr/bin/env python3
"""Prueba de arranque de SOMA en QEMU (solo lectura sobre el proyecto).

Uso:  python3 tools/boot_smoke.py [--arch x86_64|aarch64] [--timeout 30] [--root DIR]

Arranca la imagen ya compilada (build/soma.iso o build/aarch64/soma.elf) con un
disco virtual temporal, lee la consola serie y comprueba que el kernel llega a
"[SOMA ready]" sin ningun "KERNEL PANIC". No toca hdd.img.
Codigos de salida: 0 = OK, 1 = fallo de arranque, 2 = no se puede probar
(falta QEMU o la imagen).
"""
import argparse
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REQUIRED = ["KTHREADS: Inicializado", "[SOMA ready]"]
FORBIDDEN = ["KERNEL PANIC", "Fallo critico", "Triple fault"]
ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def qemu_cmd(arch: str, root: Path, disk: Path, serial: Path):
    if arch == "x86_64":
        image = root / "build" / "soma.iso"
        if not image.is_file():
            image = root / "build" / "x86_64" / "soma.iso"
        qemu = shutil.which("qemu-system-x86_64")
        cmd = [qemu, "-machine", "pc", "-m", "256M", "-no-reboot",
               "-cdrom", str(image),
               "-netdev", "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
               "-drive", f"file={disk},format=raw,if=virtio",
               "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
               "-serial", f"file:{serial}", "-display", "none"]
    else:
        image = root / "build" / "aarch64" / "soma.elf"
        qemu = shutil.which("qemu-system-aarch64")
        cmd = [qemu, "-M", "virt", "-cpu", "cortex-a72", "-m", "256M", "-no-reboot",
               "-semihosting", "-kernel", str(image),
               "-netdev", "user,id=net0", "-device", "virtio-net-device,netdev=net0",
               "-drive", f"file={disk},format=raw,if=none,id=hd0",
               "-device", "virtio-blk-device,drive=hd0",
               "-serial", f"file:{serial}", "-display", "none"]
    return qemu, image, cmd


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--arch", default="x86_64", choices=["x86_64", "aarch64"])
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--max-seconds", type=float, default=0, help="falla si llegar a [SOMA ready] tarda mas (0 = sin limite)")
    ap.add_argument("--root", type=Path, default=Path.cwd())
    a = ap.parse_args()
    root = a.root.resolve()

    with tempfile.TemporaryDirectory(prefix="soma_boot_") as td:
        td = Path(td)
        disk, serial = td / "hdd.img", td / "serial.log"
        with open(disk, "wb") as f:
            f.truncate(16 * 1024 * 1024)
        qemu, image, cmd = qemu_cmd(a.arch, root, disk, serial)
        if not qemu:
            print(f"SKIP: no hay qemu-system-{a.arch.replace('x86_64', 'x86_64')} en el PATH")
            return 2
        if not image.is_file():
            print(f"SKIP: falta la imagen {image}; compila primero con 'make ARCH={a.arch}'")
            return 2

        proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        text, t0 = "", time.time()
        try:
            while time.time() - t0 < a.timeout:
                time.sleep(0.5)
                if serial.exists():
                    text = ANSI.sub("", serial.read_text(encoding="utf-8", errors="replace"))
                if any(m in text for m in FORBIDDEN):
                    break
                if all(m in text for m in REQUIRED):
                    break
                if proc.poll() is not None:
                    if serial.exists():
                        text = ANSI.sub("", serial.read_text(encoding="utf-8", errors="replace"))
                    break
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()

        bad = [m for m in FORBIDDEN if m in text]
        missing = [m for m in REQUIRED if m not in text]
        elapsed = time.time() - t0
        if bad or missing:
            print(f"BOOT {a.arch}: FALLO ({elapsed:.1f}s)")
            if bad:
                print("  aparece:", ", ".join(bad))
            if missing:
                print("  no aparece:", ", ".join(missing))
            print("  --- ultimas lineas de la consola ---")
            for line in text.strip().splitlines()[-15:]:
                print("  |", line)
            return 1
        if a.max_seconds and elapsed > a.max_seconds:
            print(f"BOOT {a.arch}: FALLO - tardo {elapsed:.1f}s en llegar a [SOMA ready] (maximo {a.max_seconds:.0f}s)")
            return 1
        print(f"BOOT {a.arch}: OK ({elapsed:.1f}s) - llega a [SOMA ready] sin panicos")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())

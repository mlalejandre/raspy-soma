#!/usr/bin/env python3
"""Comprueba que el SO no gira en espera ocupada cuando esta en reposo (solo Linux, x86_64).

Uso:  python3 tools/idle_check.py [--max-cpu 35] [--measure 8] [--root DIR]

Arranca build/soma.iso en QEMU sin tarjeta grafica (consola de texto), espera a que
el kernel quede listo, mide la CPU del proceso QEMU durante unos segundos y exige
que quede por debajo del umbral y que el shell responda a 'ps'. Un SO con una
espera ocupada consume ~100% de un nucleo.
Codigos de salida: 0 = OK, 1 = consumo alto o sin respuesta, 2 = no se puede probar.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def cpu_seconds(pid: int) -> float:
    fields = Path(f"/proc/{pid}/stat").read_text().split(")")[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--max-cpu", type=float, default=35.0, help="maximo %% de un nucleo")
    ap.add_argument("--measure", type=int, default=8, help="segundos de medicion")
    ap.add_argument("--boot-timeout", type=int, default=60)
    ap.add_argument("--root", type=Path, default=Path.cwd())
    a = ap.parse_args()
    root = a.root.resolve()

    qemu = shutil.which("qemu-system-x86_64")
    iso = root / "build" / "soma.iso"
    if not sys.platform.startswith("linux") or not Path("/proc/self/stat").exists():
        print("SKIP: la medicion usa /proc (solo Linux)")
        return 2
    if not qemu or not iso.is_file():
        print("SKIP: falta qemu-system-x86_64 o build/soma.iso (compila primero con 'make')")
        return 2

    with tempfile.TemporaryDirectory(prefix="soma_idle_") as td:
        td = Path(td)
        disk, log = td / "hdd.img", td / "serial.log"
        with open(disk, "wb") as f:
            f.truncate(16 * 1024 * 1024)
        out = open(log, "wb")
        proc = subprocess.Popen(
            [qemu, "-machine", "pc", "-m", "256M", "-no-reboot", "-cdrom", str(iso),
             "-netdev", "user,id=n0", "-device", "virtio-net-pci,netdev=n0",
             "-drive", f"file={disk},format=raw,if=virtio",
             "-vga", "none", "-serial", "stdio", "-display", "none"],
            stdin=subprocess.PIPE, stdout=out, stderr=subprocess.DEVNULL)
        try:
            t0 = time.time()
            while time.time() - t0 < a.boot_timeout:
                if b"Modo texto activado" in log.read_bytes():
                    break
                time.sleep(0.5)
            else:
                print("FALLO: el kernel no llego a la consola de texto")
                return 1
            time.sleep(1)
            c0, w0 = cpu_seconds(proc.pid), time.time()
            time.sleep(a.measure)
            c1, w1 = cpu_seconds(proc.pid), time.time()
            pct = 100.0 * (c1 - c0) / (w1 - w0)
            proc.stdin.write(b"ps\n")
            proc.stdin.flush()
            time.sleep(2)
            responde = b"kernel_main" in log.read_bytes().split(b"Modo texto activado", 1)[-1]
        finally:
            proc.kill()
            proc.wait()
            out.close()

    estado = "OK" if (pct <= a.max_cpu and responde) else "FALLO"
    print(f"IDLE x86_64: {estado} - {pct:.1f}% de un nucleo en reposo (maximo {a.max_cpu:.0f}%), "
          f"shell {'responde' if responde else 'NO responde'}")
    return 0 if estado == "OK" else 1


if __name__ == "__main__":
    raise SystemExit(main())

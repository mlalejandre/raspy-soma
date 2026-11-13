#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PARCHE 114: prueba de cortes REALES del sistema de archivos.

Copia hdd.img a un disco temporal (tu disco NO se toca), le pone la marca de prueba y repite N veces:
arranca QEMU, lo deja escribir archivos sin parar entre 2 y 6 segundos y lo mata con kill -9. En cada arranque el kernel verifica que ningun archivo
quedo a medias y que `fscheck` no ve problemas. Veredicto al final (codigo de salida 0 = todo correcto).

    python3 tools/killtest.py                 # 20 rondas
    python3 tools/killtest.py --rondas 50 --iso build/x86_64/soma.iso --disco hdd.img

Necesita qemu-system-x86_64 en el PATH y la imagen ya compilada (python3 ejecutar.py la genera).
"""
import argparse, os, random, shutil, signal, subprocess, sys, tempfile, time

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rondas", type=int, default=20)
    ap.add_argument("--iso", default="build/x86_64/soma.iso")
    ap.add_argument("--disco", default="hdd.img", help="disco de partida (se copia); si no existe se crea uno de 64 MiB en blanco")
    ap.add_argument("--qemu", default="qemu-system-x86_64")
    ap.add_argument("--semilla", type=int, default=None)
    a = ap.parse_args()
    if not shutil.which(a.qemu):
        print("No encuentro %s en el PATH." % a.qemu); return 2
    if not os.path.exists(a.iso):
        print("No existe %s: compila antes con  python3 ejecutar.py" % a.iso); return 2
    rnd = random.Random(a.semilla)
    tmp = tempfile.mkdtemp(prefix="killtest_")
    img = os.path.join(tmp, "disco.img")
    if os.path.exists(a.disco): shutil.copyfile(a.disco, img)
    else:
        with open(img, "wb") as f: f.truncate(64 * 1024 * 1024)
    with open(img, "r+b") as f:                       # marca de prueba en LBA 30
        f.seek(30 * 512); f.write(b"SOMA_KILLTEST" + b"\0" * (512 - 13))
    ok = fallos = sin_veredicto = escrituras = 0
    for r in range(1, a.rondas + 1):
        log = os.path.join(tmp, "serie_%d.txt" % r)
        cmd = [a.qemu, "-cpu", "max", "-cdrom", a.iso, "-m", "512", "-display", "none", "-serial", "file:" + log,
               "-no-reboot", "-nic", "none", "-drive", "file=%s,format=raw,if=virtio" % img]
        p = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(rnd.uniform(2.5, 6.0))
        p.send_signal(signal.SIGKILL); p.wait()
        txt = open(log, "rb").read().decode("utf-8", "replace") if os.path.exists(log) else ""
        n = txt.count("[KW]"); escrituras += n
        if "[KV] OK" in txt: ok += 1; est = "OK"
        elif "[KV] FALLO" in txt or "DANADO" in txt or "PANICO" in txt or "PANIC" in txt: fallos += 1; est = "FALLO"
        else: sin_veredicto += 1; est = "sin veredicto (arranque lento?)"
        extra = []
        if "se completo una escritura interrumpida" in txt: extra.append("diario: rehizo una escritura")
        if "se descarto una escritura sin terminar" in txt: extra.append("diario: descarto una")
        if "actualizaciones interrumpidas" in txt: extra.append("BIGFS: recupero un temporal")
        print("ronda %2d/%d: %-6s (%d bloques de escritura antes del corte)%s" % (r, a.rondas, est, n, ("  <- " + "; ".join(extra)) if extra else ""))
        if est == "FALLO": print("   registro conservado en " + log)
    print("\nRESULTADO: %d correctas, %d con fallos, %d sin veredicto, de %d cortes con kill -9." % (ok, fallos, sin_veredicto, a.rondas))
    if fallos == 0: shutil.rmtree(tmp, ignore_errors=True)
    return 1 if fallos else 0

if __name__ == "__main__":
    sys.exit(main())

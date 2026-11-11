#!/usr/bin/env python3
"""PARCHE 056: enchufa/quita dispositivos USB en caliente en el QEMU lanzado con SOMA_USB=1|kbd|hub (monitor en build/qemu.mon).

    python3 tools/usbplug.py add [PUERTO]   anade un raton USB (por defecto en el puerto 1.3: tercer puerto del hub de SOMA_USB=hub)
    python3 tools/usbplug.py del            lo quita
    python3 tools/usbplug.py tree           'info usbhost'/'info qtree' resumido: dispositivos USB conocidos por QEMU

Los puertos son los de QEMU (el hub es el 1), no los que muestra SOMA (el mismo hub aparece alli como puerto 5).
"""
import os
import socket
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = os.path.join(ROOT, "build", "qemu.mon")


def monitor(cmds):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(3)
    try:
        s.connect(SOCK)
    except OSError as e:
        sys.exit("No puedo conectar con %s (%s). Arranca antes con SOMA_USB=hub python3 ejecutar.py y usa OTRA terminal." % (SOCK, e))
    out = b""
    def drain():
        nonlocal out
        try:
            while True:
                d = s.recv(4096)
                if not d:
                    break
                out += d
                if len(d) < 4096:
                    break
        except socket.timeout:
            pass
    drain()
    for c in cmds:
        s.sendall((c + "\n").encode())
        time.sleep(0.3)
        drain()
    s.close()
    return out.decode("utf-8", "replace")


def main():
    a = sys.argv[1:] or ["add"]
    if a[0] == "add":
        port = a[1] if len(a) > 1 else "1.3"
        print(monitor(["device_add usb-mouse,bus=xhci.0,port=%s,id=plug1" % port]) or "(enviado)")
    elif a[0] == "del":
        print(monitor(["device_del plug1"]) or "(enviado)")
    elif a[0] == "tree":
        print(monitor(["info usb"]))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()

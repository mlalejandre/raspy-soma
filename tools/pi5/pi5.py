#!/usr/bin/env python3
"""PARCHE 059: prueba SOMA en una Raspberry Pi 5 EMULADA (maquina raspi5b de QEMU), sin una Pi fisica. Todo va en Docker.

    python3 tools/pi5/pi5.py qemu      construye la imagen Docker con el QEMU raspi5b (una vez; tarda ~20-40 min)
    python3 tools/pi5/pi5.py run       compila SOMA para la Pi 5 (PLATFORM=rpi5) y lo arranca en la Pi 5 emulada (consola en esta terminal)
                                       con una tarjeta SD de prueba (sd.img) y, en PCIe1, un xHCI con hub, teclado, raton y disco USB (usb.img):
                                       prueba "pci", "usb", "usb msc", "blk", "fat ls usb0"; SOMA_PI5_USB=0|kbd para cambiarlo
                                       y la pantalla (escritorio grafico) por VNC en localhost:5900 (SOMA_PI5_GUI=0 la quita)
                                       SOMA_PI5_RP1=1: en vez del xHCI de PCIe1, un RP1 simulado en PCIe2 (como la Pi 5 real) con el mismo hub,
                                       teclado, raton y disco USB colgando de el, y su Ethernet (GEM) en la red 'user' de QEMU: ping, DNS, http (SOMA_PI5_NET=0 la quita)
                                       (hace falta la imagen con el RP1: `pi5.py qemu` la actualiza)
    python3 tools/pi5/pi5.py sd        genera soma_pi5_sd.img (FAT32 con kernel_2712.img, config.txt y DTB) para grabar en una microSD y probar en una Pi 5 real
    python3 tools/pi5/pi5.py build     solo compila (build/aarch64/kernel_2712.img y soma.elf)

Variables: SOMA_PI5_RAM=2G (1G 2G 4G 8G 16G), SOMA_PI5_CPUS=1 (1-4), SOMA_PI5_EXTRA="..." (argumentos extra de QEMU, p. ej. "-d unimp").
Salir de QEMU: Ctrl+A y luego X.
"""
import os
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOOLCHAIN = "soma-toolchain"
QEMU_IMG = "soma-pi5-qemu"


def sh(cmd, **kw):
    print("$ " + " ".join(shlex.quote(c) for c in cmd))
    return subprocess.run(cmd, cwd=ROOT, **kw).returncode


def have_image(name):
    return subprocess.run(["docker", "image", "inspect", name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def build():
    if not have_image(TOOLCHAIN):
        sys.exit("Falta la imagen Docker '%s': ejecuta antes python3 ejecutar.py una vez (la construye)." % TOOLCHAIN)
    uid, gid = str(os.getuid()), str(os.getgid())
    rc = sh(["docker", "run", "--rm", "--platform", "linux/amd64", "-u", uid + ":" + gid, "-v", ROOT + ":/soma", TOOLCHAIN,
             "sh", "-c", "make clean >/dev/null 2>&1; make ARCH=aarch64 PLATFORM=rpi5"])
    if rc != 0 or not os.path.exists(os.path.join(ROOT, "build", "aarch64", "soma.elf")):
        sys.exit("La compilacion para la Pi 5 ha fallado.")
    print("\nListo: build/aarch64/kernel_2712.img (para una Pi 5 real) y build/aarch64/soma.elf (para el emulador).")


def src_hash():
    """PARCHE 072: huella de lo que define la imagen del emulador (Dockerfile, build_qemu.sh, rp1_stub.c): si cambia, la imagen esta obsoleta."""
    import hashlib
    h = hashlib.sha256()
    base = os.path.join(ROOT, "tools", "pi5")
    for rel in ("Dockerfile", "build_qemu.sh", os.path.join("qemu", "rp1_stub.c")):
        try:
            with open(os.path.join(base, rel), "rb") as f:
                h.update(f.read())
        except OSError:
            h.update(b"-")
    return h.hexdigest()[:16]


def image_hash():
    r = subprocess.run(["docker", "image", "inspect", "-f", "{{index .Config.Labels \"soma.src\"}}", QEMU_IMG],
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return r.stdout.decode().strip() if r.returncode == 0 else ""


def image_stale():
    """True si falta la imagen o se construyo con otras fuentes (etiqueta soma.src distinta)."""
    if not have_image(QEMU_IMG):
        return True
    return image_hash() != src_hash()


def qemu_image():
    if have_image(QEMU_IMG) and not image_stale() and qemu_has_rp1():
        print("La imagen '%s' esta al dia (fuentes %s, con el RP1 simulado)." % (QEMU_IMG, src_hash()))
        return
    if have_image(QEMU_IMG):
        print("La imagen '%s' esta obsoleta (fuentes del emulador cambiadas): se reconstruye (~20-40 min)." % QEMU_IMG)
    sys.exit(sh(["docker", "build", "--label", "soma.src=" + src_hash(), "-t", QEMU_IMG, os.path.join("tools", "pi5")]))


def sd_image():
    """Tarjeta SD de prueba (64 MiB, MBR + FAT16 con ficheros, firma SOMASD--): QEMU exige un tamano potencia de 2."""
    path = os.path.join(ROOT, "sd.img")
    ok = False
    if os.path.exists(path) and os.path.getsize(path) == 64 * 1024 * 1024:
        with open(path, "rb") as f:
            head = f.read(512)
        ok = head[:8] == b"SOMASD--" and head[510:512] == b"\x55\xaa"
    if not ok:
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import mkfat
        mkfat.make_sd_test_disk(path, 64)
        print("Creada la tarjeta SD de prueba sd.img (64 MiB).")
    return "sd.img"


def web_start():
    """PARCHE 065: la pantalla se ve en el navegador (noVNC): Compartir pantalla de macOS exige contrasena y este QEMU no trae DES.
    Descarga la libreria noVNC (npm, una vez), sirve tools/pi5/web en http://localhost:6080 y abre el navegador."""
    import http.server, tarfile, threading, urllib.request, webbrowser, io
    web = os.path.join(ROOT, "tools", "pi5", "web")
    if not os.path.isdir(os.path.join(web, "core")):
        try:
            print("Descargando noVNC (una vez)...")
            data = urllib.request.urlopen("https://registry.npmjs.org/@novnc/novnc/-/novnc-1.7.0.tgz", timeout=60).read()
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as t:
                for m in t.getmembers():
                    if m.isfile() and (m.name.startswith("package/core/") or m.name.startswith("package/vendor/")):
                        dst = os.path.join(web, m.name[len("package/"):])
                        os.makedirs(os.path.dirname(dst), exist_ok=True)
                        with open(dst, "wb") as f:
                            f.write(t.extractfile(m).read())
        except Exception as e:
            print("No se pudo descargar noVNC (%s). Usa un cliente VNC en localhost:5900 (sin contrasena; Compartir pantalla de macOS no sirve)." % e)
            return
    class Q(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *a, **k): super().__init__(*a, directory=web, **k)
        def log_message(self, *a): pass
    try:
        srv = http.server.ThreadingHTTPServer(("127.0.0.1", 6080), Q)
    except OSError as e:
        print("Puerto 6080 ocupado (%s): abre la pagina que ya sirve esa direccion." % e)
        srv = None
    if srv:
        threading.Thread(target=srv.serve_forever, daemon=True).start()
    url = "http://localhost:6080/"
    print("Pantalla: %s (se abre sola en unos segundos). Teclado y raton: haz clic en ella. SOMA_PI5_GUI=0 la desactiva." % url)
    threading.Timer(5.0, webbrowser.open, [url]).start()


def rp1_mode():
    return os.environ.get("SOMA_PI5_RP1", "0") == "1"


def qemu_has_rp1():
    r = subprocess.run(["docker", "run", "--rm", QEMU_IMG, "qemu-system-aarch64", "-device", "help"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    n = subprocess.run(["docker", "run", "--rm", QEMU_IMG, "qemu-system-aarch64", "-M", "raspi5b", "-netdev", "help"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return b"soma-rp1" in r.stdout and b"user" in n.stdout           # PARCHE 069: ademas, red "user" (libslirp) para la Ethernet


def nic_args():
    """PARCHE 069: con el RP1 simulado, la Ethernet (GEM) usa la red 'user' de QEMU: IP 10.0.2.15, pasarela 10.0.2.2 (como SOMA espera) y el
    puerto 80 de SOMA (httpd) en http://localhost:8090/. SOMA_PI5_NET=0 la quita."""
    if rp1_mode() and os.environ.get("SOMA_PI5_NET", "1") != "0":
        return ["-nic", "user,hostfwd=tcp::8090-:80"]
    return []


def usb_args():
    """PARCHE 062: un xHCI en el conector PCIe1 de la Pi 5 emulada, con un hub, teclado, raton y un disco USB de prueba.
    SOMA_PI5_USB=0 lo quita; SOMA_PI5_USB=kbd deja solo teclado y raton sin hub ni disco."""
    mode = os.environ.get("SOMA_PI5_USB", "1")
    if mode == "0":
        return []
    if rp1_mode():     # PARCHE 068: el xHCI cuelga de un RP1 simulado en PCIe2; su bus USB se llama usb-bus.0
        args = ["-device", "soma-rp1,id=rp1,bus=pcie2.0"]
        bus = "usb-bus.0"
    else:
        args = ["-device", "qemu-xhci,id=xhci,bus=pcie1.0"]
        bus = "xhci.0"
    if mode == "kbd":
        return args + ["-device", "usb-kbd,bus=" + bus, "-device", "usb-mouse,bus=" + bus]
    path = os.path.join(ROOT, "usb.img")
    if not (os.path.exists(path) and os.path.getsize(path) == 8 * 1024 * 1024):
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import mkfat
        mkfat.make_usb_test_disk(path, 8)
        print("Creado el disco USB de prueba usb.img (8 MiB).")
    return args + ["-device", "usb-hub,bus="+bus+",port=1", "-device", "usb-kbd,bus="+bus+",port=1.1",
                   "-device", "usb-mouse,bus="+bus+",port=1.2", "-drive", "if=none,id=usbd0,format=raw,file=usb.img",
                   "-device", "usb-storage,bus="+bus+",drive=usbd0,port=1.3"]


def run():
    if not have_image(QEMU_IMG):
        sys.exit("Falta el emulador: ejecuta antes  python3 tools/pi5/pi5.py qemu")
    if image_stale():
        print("AVISO: la imagen del emulador no corresponde a las fuentes actuales (tools/pi5): `python3 tools/pi5/pi5.py qemu` la actualiza.")
    if rp1_mode() and not qemu_has_rp1():
        sys.exit("SOMA_PI5_RP1=1 necesita el emulador con el RP1 simulado: ejecuta antes  python3 tools/pi5/pi5.py qemu  (reconstruye la imagen, ~20-40 min)")
    build()
    sd = sd_image()
    ram = os.environ.get("SOMA_PI5_RAM", "2G")
    cpus = os.environ.get("SOMA_PI5_CPUS", "4")   # PARCHE 082: los 4 nucleos de la Pi 5
    extra = shlex.split(os.environ.get("SOMA_PI5_EXTRA", ""))
    gui = os.environ.get("SOMA_PI5_GUI", "1") != "0"
    port = os.environ.get("SOMA_PI5_VNC", "5900")
    if gui:    # PARCHE 063: pantalla de la Pi 5 emulada por VNC (puerto 5900 del Mac); la consola serie sigue en esta terminal
        disp = ["-L", "/soma/tools/pi5/share", "-display", "vnc=0.0.0.0:0,websocket=5700", "-serial", "mon:stdio"]   # -L: mapas de teclado que VNC necesita
        pub = ["-p", "5700:5700"] + (["-p", port + ":5900"] if os.environ.get("SOMA_PI5_VNC") else [])   # el 5900 (VNC clasico) solo si se pide: SOMA_PI5_VNC=5900
        web_start()
    else:
        disp = ["-nographic"]
        pub = []
    if rp1_mode() and os.environ.get("SOMA_PI5_NET", "1") != "0":
        pub = pub + ["-p", "8090:8090"]
    subprocess.call(["docker", "rm", "-f", "soma-pi5"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)   # una ejecucion anterior olvidada ya no bloquea los puertos
    qcmd = ["qemu-system-aarch64", "-M", "raspi5b,pcie2-preinit=on" if rp1_mode() else "raspi5b", "-m", ram,
           "-smp", cpus] + disp + ["-semihosting", "-kernel", "build/aarch64/soma.elf",
           "-drive", "if=sd,format=raw,file=" + sd] + usb_args() + nic_args() + extra
    if rp1_mode() and os.environ.get("SOMA_PI5_NET", "1") != "0" and os.environ.get("SOMA_PI5_GW", "1") != "0":
        # PARCHE 074: el navegador de SOMA pide las paginas https:// a 10.0.2.2:8096. Dentro del contenedor, 10.0.2.2 es el propio contenedor
        # (no el Mac), asi que la pasarela web (solo biblioteca estandar de Python) se arranca aqui, junto a QEMU. SOMA_PI5_GW=0 la quita.
        print("Pasarela web (HTTPS del navegador de SOMA): se arranca dentro del contenedor, puerto 8096.")
        inner = "python3 /soma/tools/web_gateway.py >/tmp/gateway.log 2>&1 & exec " + " ".join(shlex.quote(c) for c in qcmd)
        qcmd = ["sh", "-c", inner]
    cmd = ["docker", "run", "--rm", "-it", "--name", "soma-pi5"] + pub + ["-v", ROOT + ":/soma", QEMU_IMG] + qcmd
    sys.exit(sh(cmd))


FW_URL = "https://github.com/raspberrypi/firmware/raw/master/boot/"
CONFIG_TXT = """# SOMA para Raspberry Pi 5 (parche 074). El arranque de la Pi 5 lo hace la EEPROM: en la SD solo hacen falta este fichero, el kernel y el DTB.
arm_64bit=1
kernel=kernel_2712.img
# Consola serie por el conector UART de depuracion (115200 8N1) y mensajes de la segunda etapa
enable_uart=1
uart_2ndstage=1
# Imagen por HDMI aunque el monitor tarde en anunciarse
hdmi_force_hotplug=1
disable_overscan=1
"""


# PARCHE 079: firmware del Wi-Fi (CYW43455) para la carpeta /wifi de la SD. Son binarios de Cypress/Infineon con licencia que permite
# redistribuirlos pero no es libre: no se guardan en el repositorio, se descargan al generar la SD (los mismos que usa Raspberry Pi OS).
WIFI_URL = "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/bookworm/debian/config/brcm80211/"
WIFI_FILES = [("cypress/cyfmac43455-sdio-standard.bin", "brcmfmac43455-sdio.bin"),
              ("brcm/brcmfmac43455-sdio.txt", "brcmfmac43455-sdio.txt"),
              ("cypress/cyfmac43455-sdio.clm_blob", "brcmfmac43455-sdio.clm_blob")]


def wifi_firmware(fw):
    """Descarga (una vez) los ficheros del Wi-Fi en tools/pi5/firmware/wifi/. Devuelve [(nombre, bytes)] o [] si no se pudo."""
    import urllib.request
    d = os.path.join(fw, "wifi")
    os.makedirs(d, exist_ok=True)
    out = []
    for src, name in WIFI_FILES:
        path = os.path.join(d, name)
        if not os.path.exists(path) or os.path.getsize(path) == 0:
            try:
                with urllib.request.urlopen(WIFI_URL + src, timeout=60) as r:
                    data = r.read()
                with open(path, "wb") as f:
                    f.write(data)
                print("Descargado %s (firmware Wi-Fi, %d KiB)." % (name, (len(data) + 1023) // 1024))
            except Exception as e:
                print("AVISO: no pude descargar %s (%s). Sin el, 'wifi prueba' llega hasta identificar el chip pero no carga el firmware.\n"
                      "       Puedes copiarlo a mano de /lib/firmware/brcm/ de Raspberry Pi OS a %s." % (name, e, d))
                return []
        out.append((name, open(path, "rb").read()))
    return out


def sd_card():
    """PARCHE 074: genera soma_pi5_sd.img (MBR + FAT32) lista para grabar en una microSD y arrancar una Pi 5 real."""
    build()
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import mkfat
    kernel = os.path.join(ROOT, "build", "aarch64", "kernel_2712.img")
    fw = os.path.join(ROOT, "tools", "pi5", "firmware")
    os.makedirs(fw, exist_ok=True)
    dtb = os.path.join(fw, "bcm2712-rpi-5-b.dtb")
    if not os.path.exists(dtb):
        import urllib.request
        try:
            with urllib.request.urlopen(FW_URL + "bcm2712-rpi-5-b.dtb", timeout=30) as r, open(dtb, "wb") as f:
                f.write(r.read())
            print("Descargado bcm2712-rpi-5-b.dtb del repositorio de firmware de Raspberry Pi.")
        except Exception as e:
            print("AVISO: no pude descargar el DTB (%s). Copia bcm2712-rpi-5-b.dtb a tools/pi5/firmware/ y repite; sin el, la imagen sirve igualmente de prueba." % e)
    size_mib = 128
    d = mkfat.Disk(size_mib * 2048, 32, mbr=True, spc=1, label="SOMABOOT")
    d.add_file("config.txt", CONFIG_TXT.encode())
    d.add_file("kernel_2712.img", open(kernel, "rb").read())
    if os.path.exists(dtb):
        d.add_file("bcm2712-rpi-5-b.dtb", open(dtb, "rb").read())
    if os.environ.get("SOMA_WIFI_FW", "1") != "0":
        for name, data in wifi_firmware(fw):
            d.add_file("wifi/" + name, data)
    out = os.path.join(ROOT, "soma_pi5_sd.img")
    with open(out, "wb") as f:
        f.write(d.build())
    print("\nListo: %s (%d MiB, FAT32).\nGrabala en una microSD con Raspberry Pi Imager (Usar personalizada) o balenaEtcher, ponla en la Pi 5 y enciende.\n"
          "HDMI: conector HDMI0 (el mas cercano al USB-C). Teclado y raton: puertos USB de la Pi. Serie opcional: conector UART de depuracion, 115200 8N1." % (out, size_mib))


def main():
    a = sys.argv[1] if len(sys.argv) > 1 else ""
    if a == "qemu":
        qemu_image()
    elif a == "run":
        run()
    elif a == "build":
        build()
    elif a == "sd":
        sd_card()
    else:
        print(__doc__)


if __name__ == "__main__":
    main()

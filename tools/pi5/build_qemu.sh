#!/bin/sh
# PARCHE 059: compila el QEMU con la maquina raspi5b (fork hatter6822/rpi5_machine sobre QEMU v11.1.1) en /opt/pi5q.
# Se ejecuta DENTRO de la imagen Docker (tools/pi5/Dockerfile). Usa los espejos de GitHub porque gitlab.com no siempre es accesible.
set -eu
RPI5_COMMIT="${RPI5_COMMIT:-1d46342eecfc1be3f4ba172a83ecd73e54837811}"
cd /opt
git clone https://github.com/hatter6822/rpi5_machine pi5src
git -C pi5src checkout -q "$RPI5_COMMIT"
cd pi5src
rm -rf qemu
git clone --depth 1 --branch v11.1.1 https://github.com/qemu/qemu qemu
git clone https://github.com/qemu/keycodemapdb qemu/subprojects/keycodemapdb
git -C qemu/subprojects/keycodemapdb checkout -q f5772a62ec52591ff6870b7e8ef32482371f22c6
scripts/qemu-tree apply
# PARCHE 068: RP1 minimo (dispositivo "soma-rp1": SYSINFO + xHCI0) para probar el USB de la Pi 5 real sin placa
cp /opt/rp1_stub.c qemu/hw/arm/rp1_stub.c
sed -i "s/files('bcm2712.c')/files('bcm2712.c', 'rp1_stub.c')/" qemu/hw/arm/meson.build
sed -i '/^config BCM2712$/,/^    select ARM_GIC$/ s/^    select ARM_GIC$/    select ARM_GIC\n    select USB_XHCI_SYSBUS\n    select CADENCE/' qemu/hw/arm/Kconfig
grep -q rp1_stub qemu/hw/arm/meson.build && grep -q USB_XHCI_SYSBUS qemu/hw/arm/Kconfig
# las pruebas de coma flotante necesitan subproyectos de gitlab; no hacen falta para ejecutar
sed -i "s/^  subdir('fp')\$/  # subdir('fp')/" qemu/tests/meson.build
mkdir build && cd build
../qemu/configure --target-list=aarch64-softmmu --disable-docs --disable-user --prefix=/opt/pi5q
ninja qemu-system-aarch64 qemu-img
mkdir -p /opt/pi5q/bin /opt/pi5q/share
cp qemu-system-aarch64 qemu-img /opt/pi5q/bin/
cp -r pc-bios /opt/pi5q/share/qemu 2>/dev/null || true

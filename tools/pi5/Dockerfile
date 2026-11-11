# PARCHE 059: QEMU con la maquina "raspi5b" (Raspberry Pi 5) para probar SOMA sin una Pi fisica.
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential python3 python3-venv python3-pip ninja-build libglib2.0-dev libpixman-1-dev libfdt-dev clang lld \
    libslirp-dev device-tree-compiler git ca-certificates pkg-config flex bison && rm -rf /var/lib/apt/lists/*
COPY qemu/rp1_stub.c /opt/rp1_stub.c
COPY build_qemu.sh /opt/build_qemu.sh
RUN sh /opt/build_qemu.sh && rm -rf /opt/pi5src/build /opt/pi5src/qemu/.git
ENV PATH="/opt/pi5q/bin:${PATH}"
WORKDIR /soma

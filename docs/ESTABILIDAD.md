# Estabilidad del nucleo (parche 029)

## Que cambia
- **Virtqueue flotante** (`linker_x86_64.ld`): ya no esta clavada en `0x00800000`. Se coloca en la primera frontera de 2 MiB
  tras la imagen (hoy sigue siendo `0x00800000`), de modo que el kernel puede crecer hasta ~14 MiB. Dos `ASSERT` del enlazador
  impiden compilar si la imagen o la cola salen de los 16 MiB que el PMM reserva (`PMM_RESERVED_LOWER`).
- **BSS reducido ~1,4 MB**: la VM del REPL (`global_repl_vm`) y las dos VM de autotest (`vm.c`, `lang.c`) pasan de `.bss` al
  monticulo (la del REPL al primer uso; las de prueba solo mientras dura la prueba). Antes quedaban ~57 KB de margen; ahora ~1,5 MB.
- **Decodificadores de imagen endurecidos** (hallazgos del fuzzing con ASAN+UBSAN, `make fuzz`):
  - `web_jpeg.c`: tabla Huffman (DHT) sobresuscrita escribia fuera de `look[512]` (corrupcion de memoria del kernel con un JPEG progresivo malicioso).
  - `web_jpeg.c`: la IDCT desbordaba `int32` con coeficientes o tablas de cuantizacion corruptos (comportamiento indefinido).
  Las imagenes validas se decodifican bit a bit igual que antes (comprobado con hashes de pixeles).
- `make fuzz`: fuzzer determinista de JPEG (base y progresivo), PNG, GIF, BMP e inflate (`tests/fuzz_decoders.c`, semillas con
  `tests/fuzz_seeds.py`, requiere Pillow). `FUZZ_ITERS=20000 FUZZ_SEED=7 make fuzz` para pasadas largas.

## Lo que ya tenia el nucleo (no se toca)
Modo usuario ring 3 con CR3 por proceso, SMEP/SMAP, NX y W^X (`vmm_apply_protections`), paginas de guarda en las pilas de los hilos,
cabeceras del heap con magic + cola de control + deteccion de double free.

## Pendiente (necesita QEMU/hardware para validarse)
1. **Planificador apropiativo de hilos de kernel.** Hoy son cooperativos; el cambio toca el cambio de contexto, las secciones criticas
   (framebuffer, VFS, red, heap) y debe probarse arrancando.
2. **Aislar navegador, JS y decodificadores en modo usuario.** Hoy viven en el kernel (`src/apps/*`). Requiere una superficie de
   llamadas al sistema para framebuffer, entrada, red, VFS y portapapeles, y mover los decodificadores a un proceso con
   comunicacion por memoria compartida. Orden sugerido: decodificadores de imagen primero (interfaz pequena: bytes -> pixeles).

# Parche 030: montículo rápido, protector de pila opcional y más endurecimiento

## Montículo (src/core/mem.c)
`kmalloc` y `kfree` recorrían la lista entera de bloques en cada llamada (coste O(n)). Medido en el anfitrión con 20.000 bloques vivos:
asignar 20.000 bloques 2045 ms -> 1,5 ms; 40.000 pares kfree+kmalloc 22,98 s -> 6 ms; liberar todo 5,78 s -> 2,1 ms.
- Listas de libres por tamaño (exactas hasta 1 KiB y 4 por potencia de 2 por encima) con mapa de bits; la cabecera de 32 bytes no cambia.
- `kfree` encuentra la cabecera en O(1) (justo antes del puntero) y la valida entera: magic, tamaño, sucesor físico y límites.
  Doble free, punteros interiores y cabeceras pisadas se siguen rechazando.
- Fusión de bloques libres en O(1) por delante y por detrás (los libres llevan al final un nodo con enlaces y un "pie").
- Tamaño mínimo de bloque 32 bytes (antes 16) para que quepa el nodo. `kheap_check()` comprueba además la coherencia de las listas.
- Cambios en `tests/test_heap.c`: el veneno de memoria liberada se comprueba en los primeros 48 bytes, y `kmalloc` tras pisar una
  cabecera vecina ya no tiene por qué fallar (no recorre bloques ocupados); `kheap_check()` sigue detectándolo. Nueva prueba de estrés
  `test_estres_listas` (2.500 bloques, 3 rondas, comprobación cada 100 operaciones y coalescencia total al final).

## Protector de pila (opcional): `make SSP=1`
Activa `-fstack-protector-strong` con canario global (src/core/ssp.c) y detiene el sistema con un mensaje si una función vuelve con la pila
pisada. Por defecto está DESACTIVADO: no he podido arrancarlo en QEMU y un desbordamiento latente pasaría de silencioso a parada.
Pruébalo con `make clean && make SSP=1` (o añadiendo SSP=1 a tu comando de compilación).

## Otros
- `usermode.c` `sys_seek`: suma con desbordamiento con signo (comportamiento indefinido) sustituida por `__builtin_add_overflow`.
- `gui.c` (estrellas del escritorio): `dx*dx+dy*dy` desbordaba `int32`; el valor envuelto podía simular estrellas cercanas al cursor.
- Fuzzing del motor web (HTML/CSS/JS) con ASAN+UBSAN sobre 650 páginas mutadas: sin errores de memoria (harness cooperativo del anfitrión).

# Parche 031: red, servidor web integrado y proteccion de pila por defecto

- **DNS (src/net/dns.c):** el ID de consulta era secuencial (0x1000, 0x1001...) y el puerto local fijo (40053), y no se comprobaba quien
  respondia: cualquiera con acceso a la red del sistema podia falsificar una respuesta y redirigir el navegador. Ahora ID y puerto local
  son impredecibles por consulta y solo se aceptan respuestas del servidor DNS configurado y de su puerto 53.
- **Servidor HTTP (src/net/httpd.c):** servia cualquier ruta del VFS salvo tres patrones, asi que desde la red se podian leer
  `/etc/history.txt` (historial del shell), `/mem/conversaciones.txt`, `/etc/mem_*.txt`, `/tmp/` y los ficheros del agente. Ahora se
  rechazan `..`, `//`, `/./`, `%` y `\`, y se bloquean `/etc/`, `/mem/`, `/tmp/`, `/sys/` y `/agent/` (salvo la pizarra publica).
- **QEMU (ejecutar.py):** el reenvio del puerto 8090 estaba abierto en todas las interfaces del anfitrion (`hostfwd=tcp::8090-:80`), de
  modo que cualquier equipo de tu red local llegaba al servidor del sistema. Ahora escucha solo en `127.0.0.1`.
- **Protector de pila por defecto en x86_64** (`make SSP=0` lo desactiva). Probado arrancando con `SSP=1` sin avisos.
- `make fuzz-net`: fuzzer de la pila de red (ARP, ICMP, UDP, TCP con handshake y segmentos mutados, checksums recalculados) con
  ASAN+UBSAN (tests/fuzz_net.c). Varias pasadas largas con 6 semillas sin errores.
- Revisado y sin cambios: llamadas al sistema de usuario (`usermode.c`), analisis de respuestas DNS y de respuestas HTTP del cliente.

## Parche 032: descripcion del hardware (base para el escalado)

* `src/core/hwinfo.c`: analizadores puros de las tablas ACPI (RSDP, RSDT/XSDT, MADT, MCFG, HPET, FADT) y del arbol de
  dispositivos FDT/DTB. Cada longitud se valida y las sumas de comprobacion se verifican; no tocan hardware (reciben
  una funcion `map`), por lo que se prueban y se fuzzean en el host: `make fuzz-hw` (ASAN+UBSAN, buffers del tamano
  exacto) y `make test`.
* Comando `hw` (solo lectura): tablas ACPI, numero de CPU, LAPIC/IOAPIC, ventana PCIe ECAM y lista PCI con su clase.
* Pendiente (necesita arrancar el kernel): entregar al kernel la direccion del DTB en aarch64 (hoy `boot.s` no la guarda)
  y, con estos datos, los controladores APIC/xHCI/AHCI/NVMe (x86) y UART/GIC/timer (Raspberry Pi).

## Parche 033: DTB en aarch64, q35 y capacidades PCI

* `boot.s` (aarch64) guarda en `soma_dtb_addr` el puntero al DTB que entrega el cargador (x0). `hw` lo analiza con
  `hw_fdt_parse` y muestra modelo, CPU, RAM y UART/GIC/RTC/virtio-mmio/ECAM (con respaldo en 0x40000000 de QEMU virt).
* `SOMA_MACHINE=q35 python3 ejecutar.py` arranca x86_64 en una maquina PCIe moderna (aparece la tabla MCFG en `hw`).
* `hw -v` lista las capacidades PCI de cada dispositivo (PM, MSI, MSI-X, PCIe): requisito previo de AHCI/NVMe/xHCI.

## Parche 034: virtio-pci e IRQ de la MADT

* `hw_virtio_caps`: analiza las capacidades vendor de virtio 1.x (common/notify/isr/device/pcicfg: BAR, desplazamiento,
  longitud, multiplicador de notificacion) con recorrido acotado de la lista; probado y fuzzeado en el host.
* La MADT expone los overrides de IRQ ISA->GSI (p. ej. IRQ 0 -> GSI 2). `hw` los muestra y `hw -v` muestra las
  estructuras virtio de cada dispositivo. Base para migrar virtio-net/blk al camino moderno y activar MSI-X/IOAPIC.

## Parche 037: lectura del LAPIC / IOAPIC (comando `apic`)

* `src/arch/x86_64/apic.c`: mapea sin cache las paginas MMIO del LAPIC y del IOAPIC (direcciones de la MADT) y lee su
  estado: IA32_APIC_BASE, id/version/SVR/TPR del LAPIC y la tabla de redireccion del IOAPIC. Es SOLO LECTURA: no
  habilita el APIC ni cambia el enrutado; el comportamiento de arranque no varia.
* Siguiente fase (cuando se haya visto la salida real): rutina de interrupcion para el vector espurio y habilitacion
  opt-in del LAPIC con autotest.

## Parche 038: manejadores de vector espurio y autotest del temporizador del LAPIC (`apic test`)

* Observado en QEMU (`apic`): el firmware deja el LAPIC habilitado (SVR=0x1FF, vector espurio 0xFF) y las 24 entradas del
  IOAPIC enmascaradas: las IRQ del sistema siguen yendo por el PIC.
* La IDT tenia huecos en 0xFF: un espurio habria producido #NP. Ahora el vector 255 se ignora (sin EOI) y el 64 queda
  reservado al autotest.
* `apic test` (opt-in; no se ejecuta solo): calibra el temporizador del LAPIC contra el PIT (20 ms), programa una
  interrupcion de un disparo en el vector 64 y comprueba que llega y que el EOI funciona. Resultado: ticks/ms,
  espera y numero de IRQ. Codigos de fallo diferenciados (IF=0, PIT parado, calibracion absurda, IRQ no entregada).

## Parche 039: enrutado de IRQ por el IOAPIC (`apic route`)

* Resultado real del autotest del parche 038 en QEMU: temporizador del LAPIC a 80246 ticks/ms (divisor 16), IRQ entregada.
* `apic route test|on|off`: entrega el PIT (IRQ0 -> vector 32) y el teclado (IRQ1 -> vector 33) por el IOAPIC en lugar
  del PIC, respetando los overrides de la MADT (IRQ0 -> GSI 2). El cambio se hace con interrupciones desactivadas;
  `off` devuelve las dos lineas al PIC. `route test` activa, espera 50 ms contando las IRQ entregadas por el LAPIC
  (EOI propio) y revierte solo, con veredicto OK/FALLO. Nada se activa por defecto.
* Los manejadores de los vectores 32/33 envian ademas el EOI del LAPIC solo cuando el enrutado esta activo.

## Parche 040: AHCI (SATA) de solo lectura (`ahci`)

* Resultado real del parche 039 en QEMU: `apic route test` OK (PIT por GSI 2: 50 ticks, 50 IRQ del LAPIC, revertido al PIC).
* `src/arch/x86_64/ahci.c`: localiza el controlador (PCI 01:06:01), activa memoria y bus-master, mapea el ABAR (sin cache),
  detiene/inicia el puerto con las secuencias de la especificacion, ejecuta IDENTIFY DEVICE y READ DMA EXT del sector 0
  con DMA en memoria contigua (CLB 1 KiB, FIS 256 B, tabla de comandos alineada, una entrada PRDT). Sondeo con plazos
  (sin interrupciones); libera la memoria y detiene el puerto al terminar. SOLO LECTURA y opt-in (comando `ahci`).
* Prueba en QEMU: `SOMA_SATA=1 python3 ejecutar.py` (fuerza q35 y anade `sata.img`, 8 MiB, con 'SOMASATA-TEST-01' en el
  sector 0) y escribir `ahci`.

## Parche 041: AHCI con lectura/escritura y autotest (`ahci test`, `ahci read`)

* Resultado real del parche 040 en QEMU q35: controlador en PCI 0:31.2, 6 puertos (disco en el 0, CD en el 2), IDENTIFY
  correcto (QEMU HARDDISK, 8 MiB, LBA48) y sector 0 leido por DMA.
* El puerto queda en marcha tras la primera sonda y su memoria DMA (64 KiB de datos) es persistente. API:
  `ahci_read`, `ahci_write`, `ahci_flush` (READ/WRITE DMA EXT y FLUSH CACHE EXT, hasta 128 sectores por orden, con
  comprobacion de limites). Sigue en sondeo: las interrupciones por PCI requieren resolver el enrutado (_PRT/MSI) y se
  dejan para un parche posterior.
* `ahci test`: SOLO escribe si el disco es el de prueba de QEMU (modelo "QEMU HARDDISK" y firma SOMASATA en el sector 0).
  Escribe un patron en 8 sectores (LBA 1024), vacia la cache, lo lee y compara, restaura el contenido original, comprueba
  que el sector 0 no cambio y mide la lectura secuencial de 1 MiB. `ahci read <lba>` muestra 64 bytes de un sector.

## Parche 042: correccion de un desbordamiento de pila en `ahci_probe` (detectado por el protector de pila)

* Con el parche 041, `ahci_probe` pasaba `o->sector0` (16 bytes) a `ahci_read`, que copia siempre `count * 512` bytes: la
  pila quedaba pisada y el protector de pila detuvo el sistema. Ahora se lee a un buffer estatico de 512 bytes y se copian
  16. Revisados todos los llamadores de `ahci_read` (autotest y shell): buffers de tamano suficiente. El autotest usa
  ademas `struct ahci_info` estatica. Es el protector de pila (parche 031) haciendo su trabajo.

## Parche 043: controlador NVMe (`nvme`)

* Resultado real del parche 042 en QEMU q35: `ahci test` OK (escritura, verificacion, restauracion y sector 0 intacto).
  Causa del fallo del 041: `ahci_probe` leia 512 bytes sobre un campo de 16 (el protector de pila lo detuvo).
* `src/core/nvme_proto.c`: logica PURA del protocolo (colas de envio/finalizacion con bit de fase, identificadores de
  orden, PRP con lista, codificacion de lectura/escritura). Probada en el host (`make test`, `make fuzz-nvme`, ASAN+UBSAN)
  contra un controlador simulado: 200 operaciones aleatorias de 1..128 sectores con mas de tres vueltas de cola, error
  de dispositivo, cola llena y casos limite de PRP.
* `src/arch/x86_64/nvme.c`: localiza PCI 01:08:02, mapea BAR0 (puede ser de 64 bits por encima de 1 GiB), desactiva y
  reactiva el controlador (CC/CSTS con plazo), crea las colas de administracion y de E/S (16 entradas), IDENTIFY de
  controlador y de espacio de nombres 1 (exige sectores de 512 bytes), y ofrece nvme_read/nvme_write/nvme_flush por
  sondeo con memoria DMA contigua persistente. Autotest de escritura solo en el disco de prueba (modelo QEMU y firma
  SOMANVME). Comando `nvme [test | read <lba>]`; prueba: `SOMA_NVME=1 python3 ejecutar.py`.

## Parche 044: NVMe — colas alineadas a pagina

* Observado en QEMU con el parche 043: el controlador se detectaba (PCI 0:5.0, version 1.4, colas hasta 2048) pero
  `CSTS.RDY` no llegaba a 1. Causa: ASQ/ACQ (y las colas de E/S con PC=1) deben estar alineadas a la pagina del
  controlador (4 KiB); el 043 colocaba la cola de finalizacion en +0x400 de la misma pagina. Cada cola ocupa ahora su
  propia pagina (22 marcos DMA) y un `_Static_assert` impide reintroducir el error.
* Si el controlador no se activa, ahora se deja desactivado (CC.EN=0) y se libera la memoria DMA, y el plazo maximo baja
  a 4 s para no disparar el watchdog.

## Parche 045: capa comun de dispositivos de bloque (`blk`)

* Resultado real del parche 044 en QEMU: `nvme test` OK (QEMU NVMe Ctrl, 8 MiB; escritura, verificacion, restauracion y
  sector 0 intacto; 512 KiB leidos en 3 ms). Causa del fallo del 043: colas de administracion sin alinear a pagina.
* `src/core/blk.c`: registro de dispositivos (virtio-blk, AHCI, NVMe) con lectura/escritura troceadas en peticiones de
  128 sectores, comprobacion de limites con aritmetica sin desbordamiento y proteccion de escritura: solo los discos de
  PRUEBA de QEMU (modelo QEMU + firma SOMASATA/SOMANVME) aceptan escrituras; el disco del sistema (vda) y cualquier
  disco real quedan en solo lectura. `blk_copy` copia entre dispositivos con verificacion por relectura y rechaza
  intervalos solapados en el mismo dispositivo. Probado en el host con ASAN+UBSAN (`make test`, tests/test_blk.c).
* Comando `blk`: lista, `blk read <dev> <lba>`, `blk copy <orig> <lba> <dest> <lba> <n>` y `blk test` (copia 32 KiB de
  vda a un disco de prueba, verifica y restaura).

## Parche 046: USB xHCI, fase 1 (enumeracion)

* Resultado real del parche 045 en QEMU: `blk` y `blk test` correctos (virtio, AHCI y NVMe con la misma interfaz).
* `src/core/xhci_proto.c`: logica PURA del protocolo: anillos de TRB con bit de ciclo y TRB de enlace con Toggle Cycle,
  anillo de eventos, etapas de una transferencia de control (setup con IDT + datos + estado), contexto de entrada de
  Address Device (contextos de 32 o 64 bytes) y analizador del descriptor de configuracion USB (interfaces y extremos).
  Probada en el host con ASAN+UBSAN (`make test`, `make fuzz-xhci`): 1000 TRBs con mas de 60 vueltas de anillo, 100
  eventos, ambos tamanos de contexto y 20000 descriptores aleatorios/truncados.
* `src/arch/x86_64/xhci.c`: localiza PCI 0C:03:30, mapea BAR0 (64 KiB), reclama el controlador al BIOS (USB Legacy
  Support), lee los protocolos por puerto (USB2/USB3), detiene y reinicia (HCRST/CNR), configura DCBAA (con scratchpad),
  anillo de comandos y de eventos (sin interrupciones: sondeo) y arranca. Por cada puerto con dispositivo: reinicio del
  puerto (respetando los bits RW1C de PORTSC), Enable Slot, Address Device, lectura de 8 bytes para conocer el tamano de
  paquete de EP0 (Evaluate Context si difiere), descriptor de dispositivo y de configuracion 0. No instala controladores
  de clase todavia (fase 2: teclado HID por transferencias de interrupcion).
* Comando `usb`; prueba en QEMU: `SOMA_USB=1 python3 ejecutar.py` (anade `qemu-xhci`, un hub y un pendrive; sin teclado/raton USB, porque QEMU les redirige toda la entrada del anfitrion y
  SOMA aun no tiene controlador HID; `SOMA_USB=kbd` los anade para la fase 2).
  Esta fase NO se pudo ejecutar en QEMU al prepararla (solo compilada y con la capa de protocolo probada en el host).

## Parche 047: USB xHCI, fase 2 (teclado y raton HID)

* Resultado real del parche 046 en QEMU: `usb` enumera el pendrive (USB3, clase 08/06/50, extremos bulk) y el hub (clase 09).
  Anotado: con `usb-kbd`/`usb-mouse` conectados QEMU les redirige TODA la entrada del anfitrion y el PS/2 se queda sin
  teclado; por eso `SOMA_USB=1` no los anade y esta fase se prueba con `SOMA_USB=kbd`.
* `src/core/hid_proto.c`: logica PURA de HID boot: informe de teclado (8 bytes) -> scancodes PS/2 del juego 1 (letras,
  numeros, F1-F12, flechas, teclado numerico, modificadores con E0 cuando toca), con orden mod-pulsado / tecla-soltada /
  tecla-pulsada / mod-soltado, ignorando el informe de desbordamiento; informe de raton (botones, dx, dy, rueda).
  `xhci_proto.c` gana control sin datos, intervalo de extremo (FS/LS: ms -> potencia de 2 de microtramas; HS/SS) y el
  contexto de entrada de Configure Endpoint para un extremo de interrupcion IN. Todo probado en el host con ASAN+UBSAN
  (`make test`, `make fuzz-hid`): 50000 pares de informes aleatorios, todos los usages y todos los limites de salida.
* `src/arch/x86_64/xhci.c`: `xhci_hid_start` localiza una interfaz HID boot de teclado y otra de raton (ya enumeradas),
  hace Configure Endpoint, SET_CONFIGURATION, SET_PROTOCOL(boot) y SET_IDLE, y lanza el hilo `usbd`, que sondea el anillo de
  eventos cada 2 ms, reencola el TRB de cada informe y entrega: teclado -> `keyboard_inject_scancode` (mismo camino que el
  IRQ 1: teclas muertas, Ctrl, mayusculas, flechas...), raton -> `mouse_inject` (mouse_poll lo devuelve como un paquete mas).
  El teclado USB no repite solo: el hilo repite la tecla tras 500 ms cada 35 ms. Si un extremo da error (STALL, etc.) se
  marca muerto y se cuenta; recuperarlo (Reset Endpoint) queda para la fase 3.
* `console.c`: el cuerpo del IRQ 1 pasa a `keyboard_scancode()`, compartido con `keyboard_inject_scancode()` (que desactiva
  interrupciones mientras toca el estado). Con PS/2 el comportamiento es identico.
* Comando `usb hid [on | off | status]`. Prueba: `SOMA_USB=kbd python3 ejecutar.py`, escribir `usb hid` (con el teclado del
  Mac, QEMU lo envia ahora al teclado USB emulado). Mientras el HID esta activo `usb` no reenumera (reiniciaria el controlador).

## Parche 048: teclado/raton USB al arrancar

* Observado con el parche 047: con `SOMA_USB=kbd` QEMU redirige el teclado y el raton del anfitrion a los dispositivos USB
  emulados, y el escritorio no tiene forma de ejecutar `usb hid` (la consola serie la recoge la barra del agente como
  mision). Ahora `xhci_autostart()` (llamada desde `kernel_main`, antes del escritorio) enumera el xHCI y activa el HID
  si hay teclado o raton USB boot. Sin controlador xHCI no escribe nada. Si existe `/etc/nousb` se omite (valvula de
  seguridad para hardware real). Mientras el HID esta activo `usb` no reenumera: `usb hid off` y luego `usb`.

## Parche 049: USB xHCI, fase 3 (almacenamiento masivo)

* Resultado real del parche 048 en QEMU: `USB: teclado/raton HID activos (teclados 1, ratones 1)` al arrancar; `usb hid
  status` con 561 informes, 21 scancodes, 540 movimientos de raton y 0 errores.
* `src/core/msc_proto.c`: logica PURA de Mass Storage Bulk-Only (CBW/CSW con firma y etiqueta) y de las ordenes SCSI minimas
  (INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10), READ/WRITE(10), SYNCHRONIZE CACHE); `xhci_proto.c` gana el
  contexto de Configure Endpoint para un par de extremos bulk. Probado en el host con ASAN+UBSAN (`make test`, `make fuzz-msc`).
* `src/arch/x86_64/xhci.c`: ahora hay UN solo consumidor del anillo de eventos (`ev_pump`): atiende los eventos HID y deja
  los demas en una bandeja donde `wait_event` los recoge por ranura y extremo, asi el hilo `usbd` y las transferencias
  bulk conviven. `xhci_msc_start` configura los dos extremos bulk, SET_CONFIGURATION, INQUIRY (solo discos), TEST UNIT READY
  con reintentos y limpieza de sentido, READ CAPACITY (solo sectores de 512 bytes y menos de 2 TiB) y lee el sector 0.
  Cada orden es CBW + una etapa de datos de UN solo TRB (buffer de 64 KiB alineado a 64 KiB: 128 sectores) + CSW; un STALL o
  error de transaccion se recupera con Reset Endpoint + Set TR Dequeue Pointer + CLEAR_FEATURE(ENDPOINT_HALT).
* Se activa solo en el arranque (despues del HID, mensaje `USB: almacenamiento masivo ...`) o con `usb msc`; `blk` registra
  `usb0` y `blk test` puede usarlo como destino. Los discos USB son de SOLO LECTURA salvo el de prueba de QEMU (fabricante
  QEMU y firma `SOMAUSB-` en el sector 0: `SOMA_USB=1` o `kbd` crean/actualizan `usb.img`, ahora de 8 MiB, con esa firma).

## Parche 050: lector FAT12/16/32 (`fat`)

* Resultado real del parche 049 en QEMU: el disco USB se enumera al arrancar (`QEMU HARDDISK`, 8 MiB), `blk` lo lista como
  `usb0` y `blk test` (vda -> usb0, 32 KiB, verificado y restaurado) da OK; el HID sigue sin errores (244 informes, 0 errores).
* `src/core/fat.c`: lector de SOLO LECTURA de FAT12, FAT16 y FAT32 sobre cualquier dispositivo de 512 bytes por sector: disco
  sin particiones o MBR (primera particion FAT; GPT se reconoce y se rechaza con un mensaje claro), nombres largos (LFN con
  comprobacion de suma y UTF-8, acentos incluidos), nombres 8.3 con su capitalizacion, rutas sin distinguir mayusculas,
  ficheros fragmentados y lecturas con desplazamiento. Todo dato del disco se valida (BPB, tamano de la FAT, clusters, cadenas
  con limite de longitud) y toda E/S pasa por un callback con limites.
* `tools/mkfat.py`: constructor de imagenes FAT12/16/32 (con y sin MBR, nombres largos, fragmentacion); las imagenes de prueba
  se verifican ademas con un lector independiente (`pyfatfs`) para no depender de que el constructor y el lector coincidan
  en un mismo error. `make test` genera las imagenes y ejecuta `tests/test_fat.c`: cada fichero y directorio del manifiesto
  (tamano y CRC32), lecturas troceadas de tamanos irregulares, mayusculas distintas, errores de ruta y unas 1500 imagenes
  danadas con ASAN+UBSAN (`make fuzz-fat` hace 6000 por tipo).
* Comando `fat`: `fat` (que hay en cada disco), `fat ls <dev> [ruta]`, `fat cat <dev> <ruta>` (4 KiB), `fat get <dev> <ruta>
  <destino>` (copia al VFS, hasta 16 KiB). `SOMA_USB=1|kbd` crea `usb.img` como MBR + FAT16 con ficheros de ejemplo.
  Limite conocido: cada lectura resuelve la ruta y recorre la cadena desde el principio (suficiente para 16 KiB; un descriptor
  de fichero llegara con el acceso desde el sistema de archivos del kernel).

## Parche 051: hubs USB y recuperacion de errores del HID

* `src/core/xhci_proto.c`: Address Device con cadena de ruta y datos del hub padre (`xhci_build_addr_input_ex`), contexto de
  ranura de hub (`xhci_build_hub_input`), descriptor de hub (`usb_parse_hub_desc`) y decodificacion del estado de puerto de hub
  (`usb_decode_port_status`). Probado en el host con ASAN+UBSAN (`tests/test_hid.c`).
* `src/arch/x86_64/xhci.c`: la enumeracion se divide en `enum_device` (un dispositivo) y `hub_scan` (recursivo hasta 4 niveles):
  SET_CONFIGURATION, descriptor de hub, Configure Endpoint con el bit de hub, alimentacion de puertos, reinicio de cada puerto
  con algo conectado y enumeracion de lo que cuelga. Los dispositivos LS/FS tras un hub de alta velocidad usan su traductor
  (TT). Hasta 8 dispositivos. Hubs SuperSpeed: aun no soportados (el comando `usb` lo indica). El anillo de comandos lleva un
  cerrojo porque ahora el hilo `usbd` tambien emite comandos.
* HID: un STALL o error de transaccion ya no mata el dispositivo; `usbd` hace Reset Endpoint + Set TR Dequeue + CLEAR_FEATURE(HALT)
  y vuelve a encolar el informe (hasta 8 veces por dispositivo). `usb hid status` muestra `recuperados`.
* `usb` muestra la ruta (`Puerto 1.2` = puerto raiz 1, puerto 2 del hub) y el numero de puertos de cada hub.
* `SOMA_USB=hub python3 ejecutar.py`: teclado y raton colgando del hub (1.1 y 1.2) y el pendrive en el puerto 2. NO se ha
  ejecutado en QEMU todavia: solo compilado (x86_64 y aarch64) y probado el nivel de protocolo en el host.

## Parche 052: hot-plug de puertos raiz, diagnostico de arranque y `usb rescan`

* Resultado del parche 051 en QEMU (arranque con `SOMA_USB` sin teclado): `xHCI activo, sin teclado ni raton HID conectado` y
  disco de 8 MiB detectado. Para probar los hubs hay que arrancar con `SOMA_USB=hub`.
* El arranque ahora imprime una linea por dispositivo (`USB:   puerto 1.2: ID, clase, interfaces`, o el fallo) para ver de un
  vistazo que se enumero y que no, incluidos los hubs y sus puertos.
* Hilo `usbmon`: cada 250 ms mira que puertos raiz tienen algo conectado; si el conjunto cambia y se mantiene 500 ms, repite
  la exploracion completa (`xhci_rescan`: parar HID, reiniciar el controlador, enumerar, reactivar HID y disco). `usb rescan` lo
  hace a mano. Con el HID en marcha, `usb` ya no se niega: muestra la ultima exploracion sin reiniciar el controlador.
* Limites conocidos: solo se vigilan los puertos RAIZ (enchufar algo en un hub ya conectado requiere `usb rescan`); durante la
  reexploracion el teclado/raton se pierden ~1 s; si cambia el disco USB, `usb0` en `blk` conserva la capacidad anterior hasta
  reiniciar. La integracion de FAT en el VFS del kernel queda pendiente: el VFS actual (64 inodos, RAM) no tiene ganchos de
  montaje; hace falta rediseñarlo antes.
* `fat ls` alinea los tamanos y `fat` sin argumentos suficientes muestra el uso.

## Parche 053: diagnostico de hubs

* Resultado del 052 en QEMU con `SOMA_USB=hub`: el hub (0409:55AA, puerto 5) se enumera pero la configuracion de hub falla
  (`no configurable`) y por eso no se llega a teclado ni raton. El arranque ahora imprime el paso que falla (2 SET_CONFIGURATION,
  3 descriptor de hub, 4 Configure/Evaluate Context), el error y los codigos de finalizacion del controlador.
* El descriptor de hub se pide dos veces antes de rendirse, y si Configure Endpoint falla se prueba Evaluate Context.

## Parche 054: descriptor de hub con bLength mayor que lo leido

* Causa del fallo de hubs en QEMU (paso 3 del 053): el hub devuelve un descriptor con bLength = 11 (8 puertos) y se pedian 9
  bytes; el analizador exigia bLength <= bytes leidos. Ahora solo exige los 7 primeros bytes (los unicos que se usan).

## Parche 055: hot-plug detras de hubs

* Resultado del 054 en QEMU: hub de 8 puertos enumerado (5), teclado en 5.1 y raton en 5.2 activos desde el arranque, `usb rescan` OK.
* `usbmon` consulta ahora cada ~1 s el estado (GET_STATUS) de cada puerto de cada hub USB2 enumerado; si el conjunto de puertos
  con algo conectado cambia (y se mantiene 500 ms) repite la exploracion. Una lectura fallida no cuenta como cambio. La
  reexploracion espera a que termine una lectura en curso.
* `usb` ya no sugiere activar el HID cuando ya esta activo.

## Parche 056: monitor de QEMU para probar el hot-plug

* Resultado del 055 en QEMU: el arranque con `SOMA_USB=hub` sigue igual (hub de 8 puertos, teclado 5.1, raton 5.2, 0 errores) y no
  hay reexploraciones espurias.
* `ejecutar.py` deja el monitor de QEMU en `build/qemu.mon` cuando hay `SOMA_USB`; `python3 tools/usbplug.py add [puerto]`,
  `del` y `tree` (desde otra terminal) enchufan/quitan un raton USB, p. ej. `add 1.3` detras del hub (puertos de QEMU: el hub es el 1; SOMA lo muestra como 5 y el raton nuevo como 5.3). Debe verse en la consola
  `USB: cambio en los puertos, reexplorando` y un raton mas (o menos) tras la reexploracion.

## Parche 057: usbplug con los puertos de QEMU

* Prueba del hot-plug en QEMU (`usbplug.py add 1.3` / `del`): cada cambio provoca una reexploracion; tras `add` aparece el
  raton 5.3 y tras `del` desaparece, sin errores y con el teclado/raton de la interfaz en marcha.
* `usbplug.py` usaba por defecto `5.3` (numeracion de SOMA); QEMU numera por su arbol (hub = 1), asi que el valor por defecto
  pasa a `1.3`.

## Parche 058: base para la Raspberry Pi 5 (aarch64)

* Objetivo final: ejecutar SOMA en una Raspberry Pi 5 (BCM2712, GIC-400, UART PL011 de depuracion, USB xHCI en el chip RP1 por PCIe).
  Hoja de ruta: (1) arranque y consola por UART [este parche]; (2) MMU y cachés, interrupciones (GIC) y temporizador; (3) tarjeta SD
  (SDHCI), (4) PCIe de la BCM2712 + RP1 y reutilizar el controlador xHCI (que se hara independiente de la arquitectura y se probara
  en QEMU virt con `qemu-xhci`), (5) pantalla (framebuffer del firmware).
* Correccion: el build aarch64 estaba roto desde el parche 047 (`usb_hid_print` usaba `xhci_hid_status` fuera de las secciones x86).
  Los avisos anteriores de "compila en aarch64" no eran validos; ahora se compila Y se arranca en QEMU aarch64 (EL1 y EL2).
* `boot.s`: si el firmware entrega la CPU en EL2 (Raspberry Pi, o QEMU con `virtualization=on`) se baja a EL1 (HCR_EL2.RW,
  temporizador accesible, SPSR/ELR, SCTLR_EL1 de reinicio) conservando el DTB; la pila del arranque esta dentro de la imagen
  (`__stack_top`, 256 KiB) en vez de en una direccion fija de QEMU.
* `linker_aarch64.ld`: direccion base configurable (`--defsym=SOMA_BASE`); `make ARCH=aarch64 PLATFORM=rpi5` enlaza en 0x80000 y
  genera `build/aarch64/kernel_2712.img` (binario plano para la SD).
* `src/arch/aarch64/platform.h`: constantes por plataforma (UART, base de la reserva de marcos, virtio). En la Pi 5 la consola es
  el UART de depuracion PL011 en 0x107d001000 y no se buscan dispositivos virtio.
* `SOMA_EL2=1 python3 ejecutar.py -a aarch64` arranca QEMU virt con virtualization=on para probar la ruta de EL2.
* Prueba en una Pi 5 (sin verificar: no tengo una): copia `kernel_2712.img` a la particion de arranque junto al firmware y a
  `config.txt` anade `kernel=kernel_2712.img`, `enable_uart=1` y `arm_64bit=1`; conecta un adaptador USB-serie (115200 8N1)
  al conector de depuracion de 3 pines. Debe verse el arranque de SOMA por serie (sin pantalla, disco ni red todavia).

## Parche 059: Raspberry Pi 5 emulada (raspi5b)

* QEMU oficial no emula la Pi 5. Existe un fork fuera del arbol, `hatter6822/rpi5_machine` (maquina `raspi5b`, sobre QEMU 11.1.1,
  GPL-2.0), con 1-4 Cortex-A76 (entrada en EL2, PSCI), GIC-400, UART de depuracion PL011, temporizador de 54 MHz, mailbox y
  framebuffer del firmware, tarjeta SD (SDHCI con DMA), tres controladores PCIe con MSI y un conector PCIe1 para dispositivos de QEMU.
  Aun NO emula el chip RP1 (USB, Ethernet y GPIO reales de la Pi 5), y avisa de que muchos valores estan sin verificar porque el
  BCM2712 no tiene datasheet publico: es una aproximacion, no un sustituto de la placa.
* Resultado (verificado aqui con ese QEMU compilado desde fuentes): `kernel_2712.img`/`soma.elf` de SOMA (`PLATFORM=rpi5`) arranca
  en `raspi5b` desde EL2, reloj de 54 MHz, consola por UART10, y el shell responde a `help`.
* `tools/pi5/pi5.py`: `qemu` (construye la imagen Docker con ese QEMU; una vez, 20-40 min), `run` (compila SOMA y lo arranca en la
  Pi 5 emulada), `build`. Variables `SOMA_PI5_RAM`, `SOMA_PI5_CPUS`, `SOMA_PI5_EXTRA`. La construccion de la imagen Docker no se ha
  probado aqui (solo sus pasos, uno a uno, en este entorno).
* Siguiente: MMU/cachés y GIC con interrupciones; SD (SDHCI, ya emulada); PCIe1 con `qemu-xhci` como sustituto del xHCI de RP1.

## Parche 060: MMU, cachés y temporizador por interrupcion (aarch64)

* `mmu.c`: mapa de identidad con bloques de 1 GiB (granulo de 4 KiB, 48 bits): RAM como memoria normal cacheable y perifericos como
  Device. Sin MMU todo es Device en una placa real (fallan los accesos exclusivos de los cerrojos y los accesos no alineados, y todo
  va lentisimo). Se activa en `boot.s` antes de `kernel_main`.
* `gic.c`: GICv2 (el GIC-400 de la Pi 5 y el de QEMU virt) y temporizador fisico de EL1 (PPI 30) a 1 kHz: `timer_ticks` avanza por
  interrupcion; el manejador de excepciones despacha las IRQ en vez de entrar en panico. Autocomprobacion al arrancar:
  `GIC: temporizador por interrupcion OK (MMU y cachés activas)`.
* Direcciones fijas de QEMU virt que fallaban en la Pi 5 (el heap en 0x41000000, las pilas de hilos en 0x43000000) pasan a
  `platform.h` (`SOMA_HEAP_START`, `SOMA_THREAD_STACK_BASE`). Sigue sin haber planificador apropiativo.
* Verificado en QEMU virt (EL1 y EL2) y en `raspi5b` con 1 y 4 CPU (solo arranca la CPU 0; las demas esperan).

## Parche 061: tarjeta SD (SDHCI) en la Pi 5 + blk/fat en aarch64

* `sd_proto.c` (puro, con `tests/test_sd.c` bajo ASAN/UBSAN): palabra de orden de CMD, divisor de reloj, registro de reloj y capacidad
  desde el CSD v1/v2.
* `sdhci.c`: controlador SDHCI por PIO (SDIO1 de la Pi 5, `SOMA_SD_BASE`): CMD0, CMD8, ACMD41, CMD2/3/9/7, reloj 400 kHz -> 25 MHz,
  lectura/escritura de un bloque (CMD17/CMD24). Registros de 8/16 bits accedidos como lecturas-modificaciones de 32 bits alineados.
* `blk_glue.c`: `blk_scan` registra `vda` (virtio) y `sd0` (SDHCI). Escritura solo si la tarjeta lleva la firma `SOMASD--`
  (disco de prueba); una tarjeta real queda en solo lectura.
* `pi5.py run` crea `sd.img` (64 MiB, MBR + FAT16) y lo conecta con `-drive if=sd`. Prueba: `blk`, `blk test`, `fat`, `fat ls sd0`,
  `fat cat sd0 LEEME.TXT`.
* Verificado en `raspi5b` emulada; QEMU virt aarch64 y x86_64 siguen compilando/arrancando. Los registros reales de la Pi 5 no se han
  comprobado en placa.

## Parche 062: PCIe en la Pi 5 y USB (xHCI) en aarch64

* `pcie.c` (aarch64, `PLATFORM=rpi5`): root complex Broadcom STB de PCIe1 (el conector externo): reset del puente, PERST# liberado,
  espera del enlace, ventana de salida (CPU `0x18_0000_0000` + 1 GiB -> PCI 0), ventana de entrada (PCI `0x10_0000_0000` + 64 GiB ->
  RAM desde 0, como Linux en la Pi 5), configuracion por `EXT_CFG_INDEX`/`EXT_CFG_DATA`, enumeracion recursiva de buses (puentes y
  switches), reparto de BAR de memoria y ventanas de puente. `pci` lista los dispositivos y sus BAR.
* `pcie_proto.c` (puro, `tests/test_pcie.c` con ASAN/UBSAN): tamano y reparto de BAR, registros de ventanas, nombres de clase.
* `xhci.c` es ahora el mismo para x86_64 y aarch64: lo propio de cada arquitectura (pausa, barreras, interrupciones, DMA = direccion
  fisica + `SOMA_PCIE_DMA_PCI`, direccion PCI -> CPU del BAR, mapeo) va en `xhci_arch.h`; el fichero sigue en `src/arch/x86_64`
  (`src/arch/aarch64/xhci.h` lo reutiliza). aarch64 gana `pmm_alloc_contig`, `pci_config_write32` y la cola de teclado de la consola
  (`keyboard_inject_scancode`); el raton USB se acepta y se descarta hasta que haya escritorio.
* `blk` registra tambien `usb0` en aarch64. `fat cat` ya no estropea los bytes UTF-8 de continuacion (acentos).
* El banner de `neofetch` deja de decir "Ring 0 / virtio-blk / VirtIO-NET" en la Pi 5.
* `pi5.py run` conecta por defecto un xHCI con hub, teclado, raton y un disco USB (`usb.img`) en PCIe1 (`SOMA_PI5_USB=0|kbd`).
* Verificado en `raspi5b` emulada: enlace, enumeracion, xHCI directo y detras de un switch PCIe, hub + teclado + disco USB, teclas
  enviadas por el monitor de QEMU escritas en la consola, `fat ls/cat usb0`. Sin dispositivo: "PCIe: sin enlace" y arranque normal.
  x86_64 (hub + teclado + disco USB) y QEMU virt aarch64 siguen igual.
* NO verificado en una placa real: la Pi 5 real lleva el USB en el RP1 (detras de PCIe2, protocolo RP1 propio), no en un `qemu-xhci`
  de PCIe1; registros/ventanas del BCM2712 salen del emulador y de Linux. Los buffers DMA estan en memoria cacheable: en hardware sin
  coherencia de E/S harian falta mapas finos (memoria no cacheable) o mantenimiento de cache. Solo CPU 0, sin interrupciones MSI (sondeo).

## Parche 063: pantalla de la Pi 5 (framebuffer del VideoCore) y escritorio grafico en aarch64

* `mbox_proto.c` (puro, `tests/test_mbox.c` con ASAN/UBSAN y 200 000 respuestas aleatorias): construye la peticion de propiedades del
  firmware (tamano fisico y virtual, 32 bits, orden de pixeles, reservar buffer, pitch) e interpreta la respuesta con comprobaciones.
* `rpi_fb.c`: llamada al mailbox (`SOMA_MBOX_BASE`, canal 8) con mantenimiento de cache (`dc civac` antes y despues, el firmware lee y
  escribe RAM) y `rpi_fb_set_mode`: si el firmware no da memoria para el tamano pedido (1920x1080 no cabe en los 3 MiB del emulador)
  prueba 1280x720, 1024x768 y 800x600.
* `fb.c`: backend aarch64 de `fb_set_mode` (el resto del dibujo, doble buffer y rectangulos sucios es comun); `FB_SYNC` limpia la
  cache de las lineas copiadas a la pantalla. El raton USB mueve el cursor de la GUI y el teclado USB escribe en ella.
* `pi5.py run` muestra la pantalla por VNC (`open vnc://localhost:5900`; `SOMA_PI5_GUI=0` la quita, `SOMA_PI5_VNC=<puerto>`).
* Verificado en `raspi5b` emulada con capturas de pantalla (`screendump`): escritorio a 1024x768 con los colores correctos y "hola"
  escrito con el teclado USB. NO verificado: el cursor del raton en pantalla, el orden de pixeles y la cache en una placa real
  (la memoria de pantalla va como cacheable y se limpia a mano), ni HDMI real (el mailbox de la Pi 5 real puede comportarse distinto
  del emulado).

## Parche 064: cursor del raton en aarch64 y mapas de teclado para VNC

* `mouse_init` en aarch64 no cargaba la imagen del cursor (solo lo hacia la rama PS/2 de x86), asi que el raton USB movia el puntero sin
  dibujarlo: ahora carga el bitmap. Verificado con capturas en `raspi5b`: tecla `x` -> iconos del escritorio y flecha del raton
  siguiendo `mouse_move`. (En el escritorio vacio el cursor se oculta a proposito hasta que hay iconos o ventanas.)
* La imagen Docker del emulador no instala los mapas de teclado de QEMU y `-display vnc` fallaba con "could not find keymap file for
  language 'en-us'". `tools/pi5/share/keymaps/{en-us,es}` (generados al compilar QEMU) se pasan con `-L`, sin reconstruir la imagen.

## Parche 065: pantalla de la Pi 5 emulada en el navegador (noVNC)

* "Compartir pantalla" de macOS exige contrasena y el QEMU de la imagen no trae el cifrado DES de la autenticacion VNC (ni se puede poner
  contrasena), asi que se queda en "Conectando...". `pi5.py run` activa ahora el WebSocket de VNC de QEMU (puerto 5700), descarga una vez
  la libreria noVNC de npm a `tools/pi5/web/`, sirve `tools/pi5/web/index.html` en http://localhost:6080 y abre el navegador.
  El puerto VNC normal (5900) sigue disponible para clientes que no pidan contrasena (TigerVNC, RealVNC Viewer).
* Verificado aqui: la descarga y el servidor local, y el WebSocket de QEMU (101 + banner RFB). No se ha probado con un navegador real.

## Parche 066: sin choques de puertos al repetir `pi5.py run`

* El contenedor se llama `soma-pi5` y se elimina antes de cada ejecucion: una ejecucion anterior olvidada ya no deja el puerto ocupado
  ("port is already allocated"). El puerto 5900 (VNC clasico) solo se publica con `SOMA_PI5_VNC=5900`; la pagina web usa el 5700.

## Parche 067: RP1 (Pi 5), primer paso: PCIe2 y mapa de perifericos

* En una Pi 5 real el USB, la Ethernet y el GPIO cuelgan del chip RP1, que es un endpoint PCIe (vendor 0x1de4) en PCIe2. `pcie.c` ahora maneja
  dos root complex (0 = PCIe1, el conector; 1 = PCIe2, base 0x1000120000, ventana de CPU 0x1f_0000_0000 como la deja el firmware con
  `pciex4_reset=0`); `pcie_select()` elige sobre cual actuan `pcie_cfg_*` y `pcie_dev_*` (por defecto el 0: nada cambia para el resto).
* `src/core/rp1_proto.{h,c}` (capa pura, `tests/test_rp1.c` con ASAN/UBSAN): offsets de los perifericos dentro de la BAR1 a partir de la
  tabla 2.3 del datasheet publico (xHCI0 0x200000, xHCI1 0x300000, Ethernet 0x100000, SYSINFO 0, ...), sin solapes y dentro de 4 MiB.
* `src/arch/aarch64/rp1.{h,c}`: `rp1_init()` levanta PCIe2, busca 1de4:0001, toma su BAR1 y lee SYSINFO; `pci` muestra el resultado.
* `xhci.c`: si no hay ningun xHCI como dispositivo PCI, usa el del RP1 (`xhci_arch_alt_bar`); `usb` lo indica como "xHCI del RP1".
* Verificado aqui: compila en x86_64, aarch64 virt y rpi5; tests del host; en el emulador `pci` y `usb` siguen igual en PCIe1, y con
  `pcie2-preinit=on` y un xHCI en `pcie2.0` se enumera PCIe2 (BAR asignada). El RP1 en si NO esta emulado: su deteccion y su xHCI no se han
  podido ejecutar. Los valores (direcciones de PCIe2, ventana, mapa de la BAR1) salen del datasheet y de Linux y no estan verificados en placa.
* Pendiente para el USB real: relojes y reseteos del RP1 (clk-rp1), MSI-X (ahora se sondea), memoria DMA no cacheable.

## Parche 068: un RP1 simulado en el emulador de la Pi 5

* Nuevo dispositivo QEMU `soma-rp1` (`tools/pi5/qemu/rp1_stub.c`, se compila dentro de la imagen Docker): endpoint PCIe 1de4:0001 con BAR0
  16 KiB, BAR1 4 MiB y BAR2 64 KiB. En la BAR1: SYSINFO en 0 (identificador de chip de ejemplo 0x20001927) y un controlador xHCI en
  0x200000 como el usbhost0 del RP1; el resto de la BAR1 lee 0. No modela Ethernet, GPIO, relojes ni MSI-X. Su DMA va por PCIe2.
* `SOMA_PI5_RP1=1 python3 tools/pi5/pi5.py run` arranca `raspi5b,pcie2-preinit=on` con el RP1 en `pcie2.0` y cuelga de el el hub, teclado,
  raton y disco USB. Hace falta reconstruir la imagen una vez: `python3 tools/pi5/pi5.py qemu` detecta que le falta el RP1 y la actualiza.
  Sin la variable todo funciona como antes (xHCI en PCIe1) y la imagen antigua sigue valiendo.
* Verificado aqui con el QEMU del arbol de trabajo: `pci` ve el RP1 (BAR0/1/2 asignadas, SYSINFO leido), `usb` arranca el xHCI del RP1
  (64 ranuras, 8 puertos) y enumera hub, teclado, raton y disco (`xHCI: OK`). Sin comprobar aun: la construccion completa de la imagen
  Docker (si se prueba el script de construccion y los `sed` sobre copias limpias de los archivos).
* Sigue sin ser un RP1 real: el emulador solo comprueba que el codigo hace lo que el datasheet dice, no que la placa lo cumpla.

## Parche 069: Ethernet de la Pi 5 (GEM del RP1)

* `src/core/gem_proto.{h,c}` (capa pura, `tests/test_gem.c` con ASAN/UBSAN): registros de la MAC Cadence GEM, descriptores de 64 bits de RX y TX,
  orden MDIO, MAC <-> registros. `src/arch/aarch64/gem.{h,c}`: driver de sondeo (anillos de 16 RX y 8 TX, DMA por PCIe2 con
  SOMA_PCIE_DMA_PCI, cache limpiada/invalidada alrededor de cada acceso). La GEM esta en `eth` = BAR1 + 0x100000 del RP1.
* `virtio_net.c` encamina sus 5 funciones a la GEM en PLATFORM_rpi5: la pila de red (ARP, IP, ICMP, UDP, TCP, DNS, HTTP, httpd) no cambia.
  El banner dice "Ethernet del RP1 (GEM)". La MAC se lee de SPADDR1 (en QEMU la de la NIC) y siempre se reescribe: escribir SPADDR1HI es
  lo que activa el filtro de direccion de la GEM.
* El RP1 simulado (`rp1_stub.c`) lleva ahora una GEM de QEMU (PHY en la direccion MDIO 1, como la Pi 5) que toma el primer `-nic`.
  `SOMA_PI5_RP1=1` anade `-nic user,hostfwd=tcp::8090-:80` (IP 10.0.2.15, pasarela 10.0.2.2); la imagen Docker lleva ahora libslirp.
* Verificado aqui (QEMU del arbol de trabajo + una pasarela en Python por `-nic socket`): `GEM: ... PHY 1, enlace arriba`, SOMA envia ARP, la
  pasarela contesta y SOMA recibe; `ping 10.0.2.2` -> `Reply from 10.0.2.2` (eco enviado y respuesta recibida por la GEM).
* NO verificado: la red `user` de la imagen Docker (hace falta reconstruirla: `pi5.py qemu`), ni nada en una placa real. Pendiente para la
  Ethernet real: reloj del RP1 para la MAC, reset del PHY por GPIO, negociacion de velocidad (hoy se fuerza 1 Gb/s duplex completo),
  interrupciones MSI-X (hoy sondeo) y memoria DMA no cacheable.

## Parche 071: DMA no cacheable y GPIO del RP1

* **Memoria de DMA no cacheable (Pi 5).** `mmu.c` parte el primer GiB en bloques de 2 MiB y marca los de un pool de 4 MiB (0x1E000000, entre el heap y la
  reserva de marcos) como memoria normal NO cacheable (MAIR indice 2 = 0x44, compartible exterior). `pmm_alloc_dma()` reparte ese pool con la capa pura
  `src/core/dma_pool.{h,c}` (probada en el host) y, si no hay pool (virt) o se agota, cae en `pmm_alloc_contig`. Lo usan el xHCI (anillos, contextos, MSC; macro
  `XHCI_DMA_ALLOC` en `xhci_arch.h`, en x86_64 sigue siendo `pmm_alloc_contig`) y la GEM. Las limpiezas/invalidaciones de cache se mantienen (inocuas en NC).
  Verificado aqui: USB (hub, teclado, disco), GEM y ping siguen igual con el pool. El emulador no distingue cacheable de no cacheable, asi que esto NO demuestra que
  evite fallos de coherencia en placa; solo que el mapa nuevo es valido y no rompe nada.
* **GPIO del RP1.** `src/core/gpio_proto.{h,c}` (+ `tests/test_gpio.c`): offsets de IO_BANK0/SYS_RIO0/PADS_BANK0 (0xD0000/0xE0000/0xF0000 en la BAR1), funcion 5 =
  SYS_RIO, bits IE/OD/PUE/PDE del pad, alias XOR/SET/CLR y el parser del comando. `src/arch/aarch64/rp1_gpio.c` + comando de shell `gpio` (`gpio`, `gpio N`,
  `gpio N in|out|0|1|toggle|up|down|nopull`). El RP1 simulado modela los tres bloques (entrada sin forzar: 1 con pull-up, 0 en el resto) y se probaron las
  operaciones. Valores del datasheet publico y de Linux, sin verificar en placa (en particular el valor de reset de los pads y los pull por defecto).

## Parche 072: selector de destino en ejecutar.py y desalojo por rodajas

* **`ejecutar.py -t/--target`.** Destinos `x86_64`, `virt` (aarch64 en QEMU virt), `pi5`, `pi5-rp1`, `pi5-img` y `pi5-qemu`; sin argumentos y con terminal
  muestra un menu. `-a/--arch` sigue valiendo. Los destinos Pi 5 se delegan en `tools/pi5/pi5.py` (mismas variables `SOMA_PI5_*`), creando antes la imagen
  `soma-toolchain` si falta.
* **`pi5.py`: imagen del emulador obsoleta.** La imagen se etiqueta con la huella de `Dockerfile`, `build_qemu.sh` y `qemu/rp1_stub.c` (`soma.src`);
  `pi5.py qemu` la reconstruye solo si la huella cambia y `pi5.py run` avisa si esta obsoleta.
* **Desalojo por rodajas (aarch64).** La IRQ del temporizador levanta `sched_need_resched` cada 10 ms; `thread_preempt_point()` cede la CPU en puntos seguros
  (bucle del shell). Es cooperativo A PROPOSITO: conmutar dentro de la IRQ reentraria en estructuras del nucleo sin bloqueos (pmm, heap, TCP). Un desalojo
  real exige antes bloquear esas estructuras. `threads` muestra el contador de rodajas cedidas.

## Parche 073: negociacion automatica del PHY de la Ethernet

* `gem_proto.{h,c}`: constantes MII (BMCR/BMSR/ANAR/ANLPAR/GBCR/GBSR), `gem_link_resolve()` (velocidad y duplex a partir de lo anunciado por ambos extremos;
  prioridad 1000 FD > 100 FD > 100 HD > 10 FD > 10 HD) y `gem_nwcfg_link()` (bits de velocidad/duplex de la MAC). Probado en el host (`tests/test_gem.c`).
* `gem.c`: reinicio del PHY por MDIO, anuncio de 10/100/1000, negociacion con espera acotada (1,5 s), reprogramacion de la MAC con lo negociado y
  reevaluacion del enlace cada segundo en `gem_link_up()` (cable conectado/desconectado). Comando `eth`: velocidad, duplex y tramas.
* Verificado en el emulador: el PHY del modelo Cadence negocia 1000 Mb/s duplex completo y el ping sigue funcionando. NO verificado en placa: el PHY real es un
  BCM54213PE en RGMII, y falta fijar el reloj/modo RGMII del RP1 por velocidad y el reset del PHY por GPIO (pin 32 del RP1, fuera de los 28 que cubre `gpio`).

## Parche 074: pasarela web en el emulador de la Pi 5 e imagen de microSD

* **Navegador sin red HTTPS en `pi5.py run`.** El navegador de SOMA pide las paginas `https://` a `10.0.2.2:8096` (pasarela `tools/web_gateway.py`). `ejecutar.py` la
  arrancaba en el Mac, pero con la Pi 5 emulada QEMU corre dentro de Docker y `10.0.2.2` es el propio contenedor, no el Mac: nadie escuchaba. Ahora `pi5.py run`
  (modo RP1) arranca la pasarela dentro del contenedor junto a QEMU (solo biblioteca estandar; sin Pillow no convierte WebP/AVIF/SVG). `SOMA_PI5_GW=0` la quita.
  Registro en `/tmp/gateway.log` del contenedor.
* **Imagen para una Pi 5 real.** `pi5.py sd` (o `ejecutar.py -t pi5-sd`) compila para la Pi 5 y genera `soma_pi5_sd.img`: MBR + FAT32 de 128 MiB con `config.txt`,
  `kernel_2712.img` y `bcm2712-rpi-5-b.dtb` (descargado del repositorio de firmware de Raspberry Pi y guardado en `tools/pi5/firmware/`). La EEPROM de la Pi 5
  arranca sin mas ficheros. Verificado: el driver FAT32 de SOMA lista la imagen en el emulador. NO verificado: arranque en una placa real.

## Parche 075: primera prueba en una Pi 5 real (pantalla negra) - consola de texto en HDMI

Resultado de la placa: pantalla de arranque del firmware y despues negro, LED verde fijo. Causas encontradas en el codigo:

* **SOMA no dibujaba nada en HDMI al arrancar**: `rpi_fb_init()` era un stub y el escritorio solo se abria con `vsoma` (por la consola serie). Ahora
  `rpi_fb.c` tiene una consola de texto en HDMI (fuente 8x8 a doble tamano, colores ANSI, desplazamiento de 8 en 8 lineas) que se activa al principio de
  `kernel_main`, antes del temporizador (el mailbox lleva limite por iteraciones): todo el arranque se ve en pantalla.
* **El firmware de la Pi 5 impone la resolucion y atiende solo parte de las etiquetas** (documentacion de Circle). Antes se exigia el tamano pedido y todas las
  etiquetas atendidas: en placa habria fallado siempre. `fb_request()` prueba (1) el tamano exacto, (2) lo que el firmware devuelva a la peticion completa y
  (3) una peticion minima (leer modo + reservar). `mbox_fb_parse_any()`/`mbox_fb_build_query()` en `mbox_proto` (probados en el host, con una respuesta tipo Pi 5).
  La consola admite 16/24/32 bits y cualquier pitch; el escritorio sigue necesitando 32 bits y filas contiguas.
* **Latido en el LED verde** (GPIO 9 de gio_aon, 0x107d517c00, activo bajo; del arbol de dispositivos de Linux, SIN verificar): lento = nucleo vivo con consola
  HDMI, rapido = vivo sin framebuffer, fijo = no llego al temporizador.

Verificado en el emulador (consola, colores, desplazamiento, `vsoma` despues). Sigue sin verificar en placa: direccion real del framebuffer (se asume < 1 GiB tras
quitar los bits altos), formato de pixel y el LED.

## Parche 076: pantalla de 16 bits en la Pi 5 real (consola con colores cambiados, `vsoma` sin arrancar)

Segunda prueba en la placa: la consola de texto del parche 075 funciona y el teclado y el raton USB se detectan, pero el texto gris salia naranja, ensanchado y solapado, y
`vsoma` decia "No se pudo activar modo grafico". Causa: el firmware de la Pi 5 no atiende la etiqueta de profundidad y su pantalla es de 16 bits (RGB565); el parche 075 suponia 32.
Escribir pixeles de 32 bits en una pantalla de 16 duplica cada pixel (el gris 0xC0C0C0 queda como 0xC0C0 en RGB565 = naranja). Ademas `rpi_fb_set_mode` rechazaba todo lo que no fuera 32 bits.

* `mbox_fb_parse_any()` deduce la profundidad de bytes por fila / ancho (pitch, o tamano/alto) cuando la etiqueta no se atendio (2 -> 16, 3 -> 24, 4 -> 32). Tests con respuestas de 16, 24 y 32.
* `fb.c` (solo aarch64): el backbuffer sigue siendo de 32 bits; al volcarlo se convierte a 16 (RGB565, rojo en los bits altos), 24 o 32 con la pitch real; el cursor tambien. En 32 bits contiguos
  sigue la ruta rapida de siempre.
* Comando `hdmi`: muestra resolucion, bits, pitch, tamano, direccion y error del framebuffer. `hdmi swap` intercambia rojo y azul (consola y escritorio) por si en placa el orden de canales
  no es el supuesto (RGB565 estandar). El orden en 16 bits NO esta verificado en placa.
* Prueba en el emulador: la bandera `EXTRA_CFLAGS=-DSOMA_FB_TEST16` pide 16 bits (orden RGB) al emulador; consola y escritorio salen bien de color. No se activa en las compilaciones normales.

## Parche 077: tercera prueba en la Pi 5 real - profundidad real de la pantalla, segundo xHCI y ventilador

* **Colores y geometria.** El firmware de la Pi 5 declara 32 bits por pixel aunque la pantalla sea de 16 (RGB565): fallo conocido (raspberrypi/firmware#1904). El parche
  076 solo deducia la profundidad si el firmware no la declaraba. Ahora manda la pitch (bytes por fila / ancho) y, si falta, el tamano reservado; la profundidad declarada
  solo se usa sin esos datos y la pitch nunca se sube por encima de la declarada. La consola no escribe mas filas de las que caben en el buffer.
  Prueba en el emulador con `EXTRA_CFLAGS=-DSOMA_FB_TEST16`, que ahora imita el fallo (pantalla de 16 bits declarada como 32).
* **USB: solo funcionaba un puerto.** El RP1 tiene dos controladores xHCI (usbhost0 y usbhost1) y solo se usaba el primero. `src/arch/aarch64/xhci2.c` compila
  `xhci.c` por segunda vez con las funciones publicas renombradas a `xhci2_*` (estado propio) para el segundo; `kernel_main` lo arranca tras el primero. Teclado, raton y
  hubs del segundo llegan al sistema; los discos USB solo por el primero. El emulador no tiene segundo controlador: sin verificar.
* **Ventilador.** GPIO45 del RP1 (banco 2) como salida fija: en marcha al arrancar (nivel bajo, por la polaridad invertida que usa Linux). Comando
  `fan | fan on | fan off | fan inv`. Sin regulacion por temperatura ni PWM. Sin verificar en placa.

## Parche 078: geometria real de la pantalla en la Pi 5 (el firmware devuelve el modo pedido sin aplicarlo)

Cuarta prueba en placa: USB (los dos controladores) y ventilador bien; el video seguia mal. La foto del escritorio (ocupaba ~83 % del ancho y ~55 % del alto, colores
azulados) cuadra con SOMA creyendo 800x600 a 32 bits sobre una pantalla real de 1920x1080 a 16: 800 pixeles de 4 bytes = 1600 pixeles reales; 600 filas de 1080.

* Causa: el firmware de la Pi 5 contesta a SET con el ancho, alto y profundidad PEDIDOS como si los aplicara, pero la pantalla sigue en el modo del monitor a 16 bits.
  Pitch (3840) y tamano reservado (4147200) si son reales. La peticion de reserva de 800x600 pasaba incluso la comprobacion estricta (3840 >= 800*4).
* `mbox_fb_consistent()` exige que ancho/alto/profundidad cuadren con pitch y tamano; `mbox_fb_fix()` recalcula si no: alto = tamano/pitch, ancho = pitch/bytes por pixel
  (el que cuadre con el tamano fisico leido aparte con GET, o 2 = RGB565 si la proporcion es razonable, o 4). `mbox_fb_parse_any()` ya no modifica una pitch declarada.
  `fb_request()` solo acepta la ruta estricta si es coherente y deja de pedir tamanos menores en cuanto detecta el eco.
* `hdmi` muestra en blanco puro (legible con cualquier formato) el modo en uso y lo que declaro el firmware (ancho, alto, bits, pitch, tamano, base, tamano fisico).
* Tests del host con los numeros de la placa (eco de 800x600, 1280x720 y 1920x1080) y entradas aleatorias. En el emulador, `EXTRA_CFLAGS=-DSOMA_FB_TEST16` imita ese
  firmware (pantalla real 1280x720 a 16 bits, respuesta con el tamano pedido y 32 bits): consola y escritorio correctos a 1280x720.
* Sin verificar en placa: el orden de canales de RGB565 (`hdmi swap` lo intercambia) y lo que devuelve GET del tamano fisico.

## Parche 079: Wi-Fi de la Pi 5, fase 1 (bus SDIO y arranque del firmware del chip)

Objetivo final: escanear y conectarse por Wi-Fi (WPA2-PSK) desde SOMA. El chip es un Cypress/Broadcom CYW43455 conectado por SDIO al segundo controlador SDHCI de la
BCM2712 (SDIO2, 0x1001100000), alimentado por WL_ON (pin 28 del GPIO de la BCM2712). No se puede emular: QEMU tiene el SDHCI pero no el chip.

* **Placa** (`placa`, tambien al arrancar): revision por el mailbox (etiqueta 0x00010002): modelo, memoria, version y fabricante. El Wi-Fi solo se intenta en modelos que lo llevan.
* **Parte pura** `src/core/sdio_proto.c` (tests/test_sdio.c): CMD52/CMD53, R4/R5, CIS (fabricante), ventana del bus interno, identificador del chip, EROM (direcciones de
  nucleos y envoltorios), tamano de los bancos de RAM del ARM CR4, base de RAM por modelo (0x198000 en el 43455), NVRAM de texto a binario (probada con el fichero real
  de Raspberry Pi OS: 2074 -> 1748 bytes), estructura compartida del firmware y codigo de revision de la placa.
* **Controlador** `src/arch/aarch64/wifi.c`: SDHCI por sondeo (1 bit, 400 kHz y luego 25 MHz), deteccion de tarjeta forzada, CMD0/CMD5/CMD3/CMD7, CCCR y CIS, funcion 1
  y tamanos de bloque, reloj ALP, chip id, EROM, reinicio de nucleos por el envoltorio AI (ARM CR4 detenido, D11 en reinicio), firmware a la RAM del chip en trozos de 64
  bytes con comprobacion de principio y final, NVRAM al final de la RAM, vector de arranque en 0, ARM en marcha, reloj HT, funcion 2 lista y estructura compartida (version,
  fallos) y lectura de la consola interna del firmware (`wifi log`, muestra la version del firmware).
* **Pines**: si el chip no contesta y los pines 30-35 estan como GPIO, se prueban las funciones 1..8 (disposicion de registros de la BCM2712 C1, con resistencias de
  subida) y se restauran si ninguna sirve. `wifi mux` muestra y cambia esas funciones a mano.
* **Firmware**: `pi5.py sd` descarga de RPi-Distro/firmware-nonfree (los mismos de Raspberry Pi OS) `brcmfmac43455-sdio.bin`, `.txt` y `.clm_blob` a
  `tools/pi5/firmware/wifi/` (fuera del repositorio: licencia de Cypress redistribuible pero no libre) y los copia a `/wifi` en la SD. `SOMA_WIFI_FW=0` lo omite.
* Ordenes: `wifi` (estado), `wifi prueba` (todo seguido, para la foto), `wifi sondear`, `wifi nucleos`, `wifi firmware`, `wifi log`, `wifi mux [n]`, `wifi rd/wr` (bus interno).
* Emulador: `placa` y `wifi prueba` terminan con "el chip Wi-Fi no contesta a CMD5" sin colgar nada. Sin verificar en placa: todo lo que va despues de CMD5.
* Fase 2 (siguiente): protocolo de control del firmware (SDPCM + BCDC sobre la funcion 2), carga del CLM, `wifi scan`; fase 3: WPA2-PSK y enganche a la pila IP
  como tercer dispositivo de red (junto a virtio-net y la Ethernet del RP1); fase 4: icono de red y redes guardadas.

## Parche 080: Wi-Fi sin placa - criptografia WPA2, saludo de 4 pasos, escaneo e interfaces de red

Todo lo del Wi-Fi que no depende del chip, verificado del todo en el host y en el emulador, para que las fases 2 y 3 en placa solo tengan que enchufarlo.

* **Criptografia** `src/core/crypto.c`: SHA-1, HMAC-SHA1, PBKDF2 (contrasena + SSID -> PMK), PRF de 802.11, AES-128 (cifrar y descifrar) y AES Key Wrap.
  Vectores oficiales: FIPS 180 (incluido el millon de 'a'), RFC 2202, IEEE 802.11i anexo H (PBKDF2 y PRF), FIPS 197 y RFC 3394.
* **Suplicante WPA2-PSK** `src/core/wpa.c`: mensajes 1-4 del saludo EAPOL-Key con CCMP (PTK, MIC HMAC-SHA1, clave de grupo desenvuelta con AES y su KDE),
  renovacion de la clave de grupo, contador de repeticion, ANonce, MIC erroneo = contrasena equivocada. Probado con un saludo completo generado por una
  referencia independiente (Python con hashlib, hmac y cryptography: `tests/wpa_vectors_gen.py`): los mensajes 2 y 4 salen identicos byte a byte. Tambien
  tramas cortadas en cada posicion (ASan/UBSan).
* **Escaneo**: resultados "escan" del firmware Broadcom (bss_info): SSID, BSSID, senal, canal y banda (chanspec 802.11ac), seguridad (abierta, WEP, WPA,
  WPA2, WPA3, mixta, Enterprise) a partir de los IE RSN/WPA; fusion por BSSID y orden por senal. Como la disposicion exacta de bss_info en memoria no esta
  confirmada, se aceptan la alineada y la empaquetada (se elige la que cuadra con la longitud).
* **Interfaces de red** `src/net/netif.c`: la pila IP sigue llamando a virtio_net_*, que ahora reenvian al dispositivo activo. Registradas por preferencia:
  Pi 5: eth0 (GEM del RP1), wlan0 (Wi-Fi, no arranca hasta la fase 3), virtio0; resto: virtio0. `netif` las lista; `netif usar <nombre>` cambia en caliente.
* Ordenes: `wpa` (autoprueba en la propia maquina, PBKDF2 ~50 ms en el emulador), `wpa psk <ssid> <clave>` (PMK, como wpa_passphrase), `netif`.
* Emulador Pi 5: eth0 operativa, ping a la pasarela antes y despues de `netif usar`; la PMK calculada en SOMA coincide con la de Python.
* Nota: en el QEMU de la maquina de desarrollo, virtio-net de aarch64 virt ya fallaba antes del parche ("RX queue failed", igual en el arbol del 079);
  el camino virtio no cambia de logica.

## Parche 081: Wi-Fi, fases 2 y 3 escritas (escaneo, conexion WPA2, wlan0) y cliente DHCP

Todo el camino desde el firmware en marcha hasta navegar por Wi-Fi, para probarlo de una vez cuando haya placa. La parte pura esta probada; la que habla con
el chip esta escrita sin hardware (QEMU no tiene el chip) y se ajustara con la primera foto.

* **Protocolo del firmware** `src/core/brcm_proto.c` (tests/test_brcm.c): cabecera SDPCM (longitud y complemento, secuencia, canal, credito), ordenes BCDC
  (id, SET, error) y cabecera de datos, iovars, parametros de "escan", SSID, clave para el chip (WLC_SET_KEY, AES-CCM), trozos de descarga del CLM,
  mascara de eventos y eventos del firmware (tipo 0x886C: tipo, estado, motivo, direccion, datos).
* **Controlador** (wifi.c): tramas por la funcion 2 (lectura de 64 bytes y el resto, escritura en un bloque), sondeo del registro intstatus del nucleo SDIO,
  credito de envio, ordenes con espera de respuesta por id, eventos y datos. `wifi_up`: MAC, version del firmware, carga del CLM (/wifi/...clm_blob), eventos,
  WLC_UP y sin ahorro de energia. `wifi scan`: lista ordenada por senal (barras, dBm, canal, banda, seguridad, nombre).
  `wifi conectar <red> <clave>` (nombre con espacios entre comillas): escaneo para saber la seguridad, PMK, wsec/wpa_auth/wpaie, SET_SSID, espera del evento,
  saludo de 4 pasos con el suplicante del parche 080 (EAPOL por el canal de datos), claves par y de grupo al chip, wlan0 como interfaz activa y DHCP.
  `wifi desconectar` devuelve la red a eth0. Solo redes abiertas y WPA2-PSK (WPA3, WEP, WPA1 y Enterprise se rechazan con un aviso).
* **DHCP** `src/net/dhcp*.c`: DISCOVER/OFFER/REQUEST/ACK (y NAK), aplica IP, mascara, puerta de enlace y DNS. La mascara ya no es fija (/24): la da el router.
  IP acepta UDP de difusion (y cualquier UDP mientras no hay direccion) y envia a 255.255.255.255 sin ARP. Orden `dhcp` (vale tambien para la Ethernet).
  Probado en la Pi 5 emulada con un servidor DHCP de prueba: IP 192.168.7.42/24, puerta 192.168.7.1, DNS 1.1.1.1 y ping a la nueva puerta de enlace.
* Puntos a confirmar en placa (los primeros sospechosos si algo falla): direccion de la FIFO de la funcion 2 (0x8000 con la ventana en chipcommon),
  lectura en dos partes, credito inicial, orden de las claves y el IE RSN que acepta el AP.

## Parche 082: los 4 nucleos (aarch64: Pi 5 real, Pi 5 emulada y QEMU virt)

Hasta ahora SOMA usaba un solo nucleo. El resto del sistema (hilos, red, sistema de ficheros, escritorio) no tiene cerrojos pensados para varios
nucleos a la vez, asi que se ha elegido un modelo asimetrico que no puede romper nada de lo que ya funciona:

* El nucleo 0 sigue llevando todo el sistema igual que antes. Los nucleos 1-3 arrancan por PSCI CPU_ON y quedan dormidos (wfe), sin interrupciones,
  esperando trabajos de calculo puro que les manda el nucleo 0 (`smp_parallel`, `src/core/smp.h`). Un trabajo no imprime, no pide memoria ni toca dispositivos.
* Arranque (`boot.s`, `src/arch/aarch64/smp.c`): SMC si el firmware deja el sistema en EL2 o es la Pi 5 (TF-A), HVC en QEMU virt sin EL2. Numero de nucleo
  en MPIDR.Aff1 (Cortex-A76 de la Pi 5, bit MT) o Aff0 (QEMU virt). El secundario baja a EL1 si hace falta, activa la MMU con las mismas tablas
  (valores escritos hasta la RAM por el nucleo 0, porque el secundario arranca con las caches apagadas) y usa su propia pila de 64 KiB.
* Usos reales: (1) el volcado de pantalla de rectangulos grandes se reparte por filas (sobre todo la conversion a 16 bits de la Pi 5 real);
  (2) PBKDF2 calcula sus bloques en paralelo (la clave de WPA2 son 2 bloques: casi la mitad de tiempo); probado en el host que da lo mismo.
* Orden `cpus` (nucleos, MPIDR, trabajos, tiempo ocupado, volcados repartidos), `cpus prueba` (SHA-1 de 6 MiB con 1 y con N nucleos),
  `cpus 1` / `cpus todos` (apaga o enciende el reparto).
* Emulador: QEMU virt -smp 4: 4 nucleos por HVC, x2,9 en la prueba; Pi 5 emulada -smp 4: 4 nucleos por SMC (MPIDR 0x81000000..0x81000300),
  x2,1; PMK de WPA2 de ~47 a ~35 ms; escritorio a 16 bits correcto con el volcado repartido. x86_64 sigue con un nucleo (`smp_cpus() = 1`).
* Siguiente paso posible: planificador simetrico (hilos en cualquier nucleo), que exige cerrojos de verdad en red, ficheros, memoria y escritorio.

## Parche 083: Paint renovado (estetica del explorador de archivos) y guardado PNG

* **Aspecto**: tema oscuro del explorador, texto suavizado, iconos vectoriales con antialias, esquinas redondeadas. Barra superior (nuevo, abrir, guardar,
  deshacer, rehacer, zoom -/+, porcentaje, ajustar, ruta), rail de herramientas, area de trabajo con sombra bajo el lienzo, panel de propiedades
  (color con codigo #RRGGBB y RGB, selector de saturacion/brillo, barra de tono, paleta de 24, recientes, grosor con deslizador) y barra de estado
  (herramienta, grosor, mensajes, posicion, tamano). El panel se adapta a la altura; en ventanas estrechas queda una tira de colores.
* **Lienzo**: 320x240 por defecto (antes 80x60), 480x360 o 640x480; zoom 25 %..800 % o ajustado; rejilla de pixeles a partir de 600 %; mano para moverse.
* **Herramientas**: lapiz, pincel redondo, aerografo, goma, cubo (relleno por tramos), cuentagotas (vuelve a la herramienta anterior), linea, rectangulo
  y elipse (contorno con grosor o rellenos) con vista previa exacta, mano. Grosor 1..32.
* **Deshacer/rehacer** de 12 pasos (incluye cambios de tamano de lienzo, abrir, pegar y cortar). Atajos: Ctrl+Z/Y/S/O/N, [ ] grosor, + - 0 zoom y letras
  de herramienta (p b a e g i l r f o u h).
* **PNG** `src/core/png_enc.c`: deflate propio (LZ77 con ventana de 32 KiB y cadenas hash, Huffman fijo) y filtro por fila. Un dibujo de 320x240 ocupa
  2-4 KiB y cabe en RamFS (16 KiB por archivo). Comprobado con zlib y PIL de Python (tests/check_png.py) y con el propio decodificador de SOMA (abrir lo
  guardado). Abre PNG, BMP, JPEG y GIF (lo transparente sobre blanco; lo grande se reduce a 640x480). BMP se sigue pudiendo guardar si cabe.
* **Arreglos de paso**: la orden `paint` abria la GUI sin la ventana (ahora usa la app pendiente como el terminal); reabrir Paint conserva el dibujo;
  RamFS sin disco virtio (Pi 5) devolvia error en cada escritura aunque el archivo quedaba en memoria: ahora es solo memoria y no falla.
* Verificado en la Pi 5 emulada con raton USB: pincel, elipse rellena, rectangulo de 13 px, cubo, linea, deshacer/rehacer, Ctrl+S (4 KiB), Ctrl+N y Ctrl+O.

## Parche 084: el Maquetador entiende y dibuja LaTeX

* **Motor** `src/apps/tex.c` (puro, sin memoria dinamica; mide el texto por una interfaz): lee el .tex y produce operaciones de dibujo
  (texto, rectangulos, lineas, imagenes) en coordenadas de pagina. Documento: titulo/autor/fecha y \maketitle, resumen, secciones numeradas
  (1, 1.1, 1.1.1, con *), \paragraph, \label/\ref/\eqref (dos pasadas: referencias hacia delante), itemize/enumerate/description anidadas,
  quote, center, verbatim/lstlisting, tabular con | y \hline (y booktabs), figure/table con \includegraphics y \caption numerado, notas al pie
  al final, negrita, cursiva, mono, subrayado, color, enlaces, tamanos, acentos, guiones y comillas tipograficas; parrafos justificados.
  Matematicas al estilo TeX: atomos con clase y su espaciado, cursiva para variables, \frac/\dfrac/\binom, \sqrt[n], indices, \sum \int
  \prod \lim con limites en modo display, \left\right con delimitadores que crecen, matrices (pmatrix, bmatrix, vmatrix, cases, aligned),
  acentos, \text, \mathbf, \mathbb, griego y ~120 simbolos; equation numerada y align con alineacion en & y \nonumber.
  Lo desconocido se muestra en rojo y se cuenta (barra de estado).
* **Maquetador** (`src/apps/layout.c`) con la estetica del explorador: barra (abrir, recargar, editar en el Bloc de notas, Documento/Fuente,
  tamano de letra, ruta), hoja blanca con sombra, rueda y barra de desplazamiento, vista Fuente con colores y numeros de linea, peticion a la
  IA abajo (el subagente "maquetador" ahora escribe LaTeX completo y se guarda en /workspace/documento.tex), estado con secciones/ecuaciones.
  Los documentos #SOMA-LAYOUT v1 antiguos se convierten a LaTeX al cargarlos.
* Integracion: doble clic en un .tex en el explorador, orden `latex [archivo.tex]`, y escritura en vivo de un .tex por el agente.
  La orden de abrir el maquetador usa la app pendiente (antes abria la GUI sin la ventana).
* Pruebas: tests/test_tex.c (estructura, numeracion, referencias, align, tabla, figura, desconocidos, desbordamiento; cortes del documento en cada
  posicion y 3000 mutaciones al azar con ASan/UBSan; se encontraron y arreglaron un bucle sin fin con texto cortado y dos accesos fuera de rango).
  Revision visual con DejaVu real en el host y en la Pi 5 emulada (rueda, vista Fuente).

## Parche 085 — Ajustes de la interfaz, punteros nuevos y "Guardar imagen como"

* **Ajustes** (`src/apps/settings.c`, orden `ajustes`, icono en el dock): transparencia de las ventanas (40-100 %, con opcion de mantener
  opaca la activa y vista previa) y fuente de la interfaz (DejaVu Sans o Sans Mono, cuatro tamanos). Se aplica al momento y se guarda en
  /etc/ui.conf; se carga al arrancar el escritorio. La fuente de la interfaz no cambia el contenido de las paginas web (WF_RAW).
* **Transparencia** (`fb.c`): la ventana de arriba se mezcla en la presentacion con lo que hay debajo comparando cada pixel con el ultimo
  mezclado (sin desvanecerse frame a frame); las de detras se mezclan al redibujar. Menus y dialogos se excluyen y quedan opacos.
* **Punteros** (`mouse.c`): dibujados como vectores 24x24 con suavizado 4x4, borde negro y sombra: flecha de cola recta, redimension
  horizontal, vertical y diagonales segun el borde, mover al arrastrar una ventana, barra de texto. Punto caliente por cursor.
* **Navegador**: boton derecho abre un menu (Guardar imagen como..., Copiar imagen, Copiar direccion de la imagen/enlace, Atras,
  Adelante, Recargar). El dialogo **Guardar imagen como** (`src/apps/savedlg.c`) deja elegir nombre, carpeta (con atajos a las existentes y
  a big:/imagenes si hay disco), formato PNG o BMP y escala 100/75/50/25 %, y avisa si no cabe en RamFS (16 KiB por archivo).
* Orden `navegador [url]`. Probado en la Pi 5 emulada: ajustes, menu sobre la imagen de la demo y guardado.

## Parche 086 — Imagenes del navegador: memoria, JPEG reducidos al decodificar, CMYK

* Diagnostico: los "PNG/JPEG no decodificable" de paginas pesadas (p. ej. 20minutos) eran en realidad **falta de memoria**: el heap del
  kernel tenia un tope fijo de 64 MiB y una pagina de noticias (DOM, estilos, 20 MiB de imagenes, pantalla 1920x1080) lo agotaba; el
  decodificador fallaba al reservar y se informaba como imagen danada. Las mismas imagenes servidas en local se ven bien.
* `mem.c`: en x86_64 el tope del heap es la mitad de la RAM (hasta 256 MiB) cuando la RAM queda por debajo de la ventana del heap;
  `ejecutar.py` arranca x86_64 con 512 MiB (`SOMA_MEM` para cambiarlo). El arranque dice el tope ("hasta N MiB").
* `web_img.c`/`web_jpeg.c`: las reservas fallidas se anotan (`wi_oom`); `wi_decode_fit` decodifica los JPEG ya reducidos 1/2, 1/4 u 1/8
  en el dominio DCT cuando se van a mostrar mas pequenos (menos memoria y mas rapido). Si aun falta memoria, el navegador libera imagenes
  lejanas y reintenta mas pequena; solo si no hay manera dice "sin memoria para decodificarla".
* JPEG CMYK e YCCK (Adobe, base y progresivo) decodificados en SOMA; croma con interpolacion bilineal (antes, bordes en escalera) para las
  fotos normales. Fuzzing ampliado con semillas CMYK y decodificacion reducida.
* Transparencia: con opacidad 100 % se liberan las dos capas de pantalla (16 MiB a 1920x1080).
* Navegador: abre grande (2/3 del ancho, 85 % del alto); `navegador URL` y abrir el explorador desde la consola abrian el escritorio sin la
  ventana (faltaba la app pendiente).

## Parche 087 — Fuera el limite de 16 KiB por archivo

* **RamFS** (`src/fs/fs.c`): cada archivo reserva en memoria lo que mide (antes 16 KiB fijos aunque tuviera 10 bytes) y puede llegar a
  32 MiB (`FS_FILE_MAX`); caben 256 archivos (antes 64).
* **Disco, compatible con lo que ya hay**: los archivos de menos de 16 KiB siguen en su ranura de SOMAFS02; los mayores, y los que pasan
  de las 64 ranuras, se guardan solos en el almacen BIGFS como `/.ramfs/NNN` (cabecera "RFS1" + contenido). La cabecera de la ranura
  lleva la marca 'B'. Si la region BIGFS esta en blanco se formatea sola la primera vez; si tiene otra cosa no se toca y el archivo
  grande queda solo en memoria (se avisa una vez). Sin disco (Pi 5) todo vive en memoria, como antes.
* API nueva: `vfs_size()`, `vfs_read_alloc()` (el archivo entero en un bufer nuevo) y `vfs_big_persist()`.
* Adaptados a archivos grandes: explorador (tamanos reales, copiar/mover entre RamFS y BIGFS enteros, abrir copias de hasta 16 MiB; antes
  `fm_read_head` perdia el ultimo byte), `cp`/`mv` (copiaban solo 4 KiB), `stat`, `du`, `fat get`, `big put/get`, `exec`, lectores por
  tuberia, navegador `file://`, visor de imagenes (tope de 64 KiB), Maquetador (imagenes y documentos de 32 KiB), Paint (PNG/BMP sin tope),
  Bloc de notas (128 KiB), lenguaje SOMA y el dialogo "Guardar imagen como" (ya no reduce la imagen ni avisa de 16 KiB).
* Ordenes nuevas: `descargar <url> [archivo]` (guarda una URL entera en un archivo) y `suma <archivo>` (tamano y CRC-32).
* Pruebas: test_fs con archivo de 300 KB, encoger por debajo de una ranura, tope de 32 MiB, 256 archivos, y persistencia entre procesos de un
  archivo de 200 KB y 90 archivos mas alla de las 64 ranuras. En x86 emulado: descargas de 573 KB y 3 MB, reinicio, CRC-32 identico.

## Parche 088 — Lector de PDF y visor de imagenes nuevo

* **Motor PDF propio** (`src/apps/pdf.c`, solo enteros: coma fija 16.16): xref clasica y en flujo (PDF 1.5), flujos de objetos,
  actualizaciones incrementales, reconstruccion si la xref esta rota; FlateDecode con predictores PNG, LZW, ASCII85, ASCIIHex, DCT (JPEG).
  Arbol de paginas con herencia (MediaBox/CropBox, Resources, Rotate). Contenido: estado grafico, cm, colores gris/RGB/CMYK, trayectos con
  curvas, rellenos par-impar y distinto de cero, trazos, texto (Tf, Td, TD, Tm, T*, Tj, TJ, ', ", Tc, Tw, Tz, TL, Ts, Tr), XObjects de
  imagen (RGB, gris, CMYK, Indexed, ICC, mascaras, SMask) y de formulario. Texto a Unicode por ToUnicode, WinAnsi, MacRoman, Standard,
  /Differences con nombres de glifo y las codificaciones de Computer Modern (TeX). Las letras salen con DejaVu en su posicion exacta (los
  anchos son los del PDF); cada fuente se escala igual para ocupar su sitio. No soporta: PDF cifrado, JBIG2/CCITT/JPX, sombreados, recortes.
* **Lector PDF** (`src/apps/pdfview.c`, icono "PDF" en el dock, orden `pdf [archivo]`, doble clic en un .pdf en el explorador): barra con
  Abrir (selector de los PDF de RamFS), paginas, zoom, ajustar al ancho / pagina, miniaturas y "Copiar texto"; hoja con sombra, rueda
  (al llegar al final pasa de pagina), arrastre y teclas (AvPag/RePag, flechas, +/-, w, f, t, c, o). La pagina se dibuja una vez en un bufer
  (`fb_target_begin/end`: las primitivas de siempre sobre un bufer propio) y desplazarse solo copia.
* **Visor de imagenes rehecho** (`src/apps/imgview.c`, orden `imagen [archivo]`): PNG, JPEG (base, progresivo, CMYK), GIF y BMP de
  cualquier tamano (antes solo BMP de hasta 64 KiB y una galeria de 16 rutas de 64 caracteres), galeria de todas las imagenes de RamFS con
  miniaturas, zoom con la rueda hacia el puntero, arrastre, ajustar / 1:1, giro, fondo (cuadros, claro, oscuro), copiar y pegar.
  Reduccion con mipmaps y bilineal (sin dientes); al ampliar, pixeles nitidos.
* Al abrir desde la consola (`navegador`, `imagen`, `pdf`) la ventana sale de verdad (app pendiente).
* Pruebas: `tests/test_pdf.c` (volcado de operaciones, comprobacion de los PDF de `tests/pdf_seeds.py` con xref clasica y en flujo,
  y `make fuzz-pdf` con ASAN/UBSAN). Revisado contra pdftoppm con PDF de ReportLab, pdfLaTeX, LibreOffice y matplotlib; el fuzzing
  encontro un fallo real (el arbol de paginas no se leia si estaba en un flujo de objetos) que queda arreglado.

## Parche 089 — Monitor del sistema

* **Monitor del sistema** (`src/apps/sysmon.c`, icono "MONITOR" en el dock, orden `monitor`): Resumen (tarjetas con grafica de CPU,
  memoria, red y pantalla; lo que mas consume; equipo), Procesos (aplicaciones y servicios con CPU y memoria, o hilos con su CPU,
  estado y pila; ordenar por CPU/memoria/nombre; cerrar una aplicacion o terminar un proceso de usuario), Memoria (anillo con el reparto,
  historial, heap por aplicacion), CPU (uso total y por nucleo, modelo, frecuencia, caches, extensiones, temperatura), Graficos (fps,
  resolucion, controlador, memoria de pantalla; en la Pi 5 frecuencias de la VideoCore VII), Almacenamiento (RamFS, BIGFS, discos), Red
  (bajada/subida, interfaces) y Hardware (placa, firmware, serie, dispositivos PCI con nombre). Una muestra por segundo, 2 minutos de historial.
* **Contabilidad** (`src/core/acct.h`): cada hilo tiene un dueno; el planificador cuenta los ciclos de CPU por hilo y por dueno, y el heap
  guarda el dueno de cada bloque (byte alto de `req`). La GUI cambia de dueno al dibujar cada ventana y al dar los eventos a la activa:
  asi se ve la CPU y la memoria de cada aplicacion aunque todas compartan el hilo de la GUI. RamFS y la pantalla tienen dueno propio.
* **Datos de hardware** (`src/arch/*/hwmon.c`): x86_64 por CPUID (marca, familia, caches, extensiones, hipervisor) y PCI; en la Pi 5 por el
  buzon del firmware (modelo, revision, serie, temperatura, tension, frecuencias ARM/GPU, memoria de la GPU).
* **La GUI ya no gira sin parar**: si no hay tecla ni fotograma pendiente cede la CPU 1 ms (el hilo inactivo para el procesador). Antes el
  bucle consumia el 100 % siempre (y un nucleo entero del anfitrion en QEMU).
* Fallo corregido: en x86_64 `src/arch/x86_64/sysinfo.h` ordenaba los campos distinto que `src/core/sysinfo.h`; quien leia la IP con
  `sysinfo_get` fuera de sysinfo.c recibia la MAC.

## Parche 090 — El Monitor se actualiza en segundo plano

* Antes solo tomaba muestras y se repintaba cuando era la ventana activa. Ahora el bucle de la GUI busca la ventana del Monitor en cada
  segundo, toma la muestra y la repinta aunque este detras: si ninguna ventana de encima la tapa (y no hay transparencia) solo se dibuja su
  contenido; si alguna la tapa, se recompone el escritorio entero para no pintar encima de la otra ventana.

## Parche 091 — El agente SOMA ya no congela el escritorio
- La mision se ejecuta en un hilo propio (`soma_agente`); el bucle de la GUI sigue moviendo, arrastrando y redibujando ventanas.
- Cuando el agente necesita la pantalla (abrir el bloc de notas, el maquetador, escribir en vivo) pide turno al bucle principal (`gui_enter/gui_leave`), que se lo concede en un punto seguro.
- "soma <mision>" desde la Terminal tambien va en segundo plano. Solo una mision a la vez.
- El despachador de ordenes del shell queda protegido por un cerrojo: la Terminal avisa en vez de bloquearse si el agente lo esta usando.
- Pilas de hilo de 32 KiB a 60 KiB.

## Parche 092 — Nombre editable de verdad en "Guardar imagen como"
- Los campos NOMBRE y CARPETA tienen cursor: flechas, Inicio/Fin, Retroceso, Supr, clic para colocar el cursor, tildes y enes (UTF-8).
- Al abrir el dialogo el nombre (sin extension) queda seleccionado: lo que escribas lo sustituye.
- Si borras la extension se anade la del formato elegido al guardar; se rechazan nombres vacios o con caracteres no validos (/ \ : * ? " < > |).

## Parche 093 — SOMA Studio: editor de codigo profesional
- Nueva pestana EDITOR (la primera) en el Coder: explorador de archivos .soma, pestana del archivo, numeros de linea,
  resaltado de sintaxis, guias de sangria, linea actual, parejas de llaves, barra de desplazamiento y barra de estado.
- Edicion: cursor y seleccion con raton (arrastre, doble clic palabra, triple clic linea) y Mayus+flechas; Inicio inteligente,
  RePag/AvPag, autoindentado, Tab/Mayus+Tab por bloques, Ctrl+K comentar, Ctrl+D duplicar, Ctrl+L linea, deshacer/rehacer
  (Ctrl+Z / Ctrl+R) y portapapeles del sistema.
- Ctrl+F buscar (incremental, con contador), Ctrl+H reemplazar (uno/todo), Ctrl+G ir a linea, Ctrl+O abrir, Ctrl+N nuevo,
  Ctrl+S guardar, Ctrl+B verificar (compilador + lint; la linea con error se marca en el texto y en el panel de problemas),
  Ctrl+E ejecutar.
- Barra "SOMA IA": pide un cambio sobre el archivo abierto; mientras trabaja el editor queda en solo lectura y al terminar el
  resultado entra como una edicion mas (Ctrl+Z la deshace).
- Orden de shell `studio [archivo]`; la ventana del Coder abierta desde la shell ya aparece al arrancar el escritorio.

## Parche 094 — SOMA Studio: nuevo aspecto visual
- Toda la interfaz del Coder usa la fuente antialiasada del sistema; rotulos en versalitas espaciadas.
- Pestanas de tipo subrayado sin caja y sin hueco bajo el titulo; insignia de estado de SOMA-LANG.
- Botones planos redondeados; Verificar y Ejecutar (con icono de reproducir) a la derecha de la barra.
- Explorador, editor, panel de problemas, barra de SOMA IA y barra de estado como tarjetas redondeadas; iconos de archivo,
  contadores en pildoras, pastilla de estado (sin errores / con errores / sin verificar).
- El explorador ya no repite archivos y, si dos se llaman igual, muestra su carpeta.
- La ventana del Coder se abre grande.
- Teclado: cada pulsacion guarda si Ctrl/Mayus estaban pulsadas; los atajos (Ctrl+F, Ctrl+A, Ctrl+D...) ya no fallan
  cuando la tecla se procesa un instante despues de soltar Ctrl.

## Parche 095 — La Pi 5 emulada a 1920x1080, como la placa real
- El VideoCore del emulador solo tenia 4 MiB de memoria de video: el escritorio caia a 1024x768. tools/pi5/build_qemu.sh
  la sube a 12 MiB (ventana hasta 1 GiB) y la Pi 5 emulada da 1920x1080, igual que la Pi real con su monitor HDMI.
- Hay que reconstruir la imagen del emulador una vez:  python3 tools/pi5/pi5.py qemu  (~20-40 min).

## Parche 096 — Programador con IA: nucleo agentico nuevo (src/apps/coder_core.c)
- Antes: el Coder delegaba en el agente generico (JSON con el programa escapado dentro de un comando write), escribia el archivo
  en cada intento y reparaba con un mensaje corto. Ahora un bucle propio guiado por el compilador:
  1. Contexto anclado: ficha de SOMA-LANG comprobada con el compilador real (una ventana sin manejador, rand(min,max), sin ++/var...),
     ejemplo verificado segun la tarea (ventana o consola), lecciones aprendidas y, al modificar, el programa actual completo.
  2. Plan breve con criterio de aceptacion (solo si la tarea lo merece).
  3. Salida en bloque ```soma``` y extraccion robusta (<think>, prosa, bloques sin cerrar).
  4. Normalizador determinista: elif, True/False/None/null, var/const, function, console.log, x++, .length, # comentarios,
     plantillas `${}`, comillas tipograficas, tabuladores, rand(n) y el manejador de ventana (win.clear(w, ...)) se corrigen sin
     gastar llamadas.
  5. Verificacion: compilador + lint + comprobaciones de la tarea (ventana pedida y no creada, falta win.present / win.active,
     programa cortado, modificacion identica al original).
  6. Reparacion con la linea exacta, contexto numerado, pista por tipo de error y el archivo completo; mejor candidato conservado;
     regeneracion desde cero si dos rondas no mejoran; parada si el modelo repite la misma respuesta; presupuesto de 7 llamadas.
  7. Revision critica final (aceptada solo si sigue compilando).
  8. El archivo se escribe UNA vez y solo si el resultado es correcto: un fallo o una cancelacion no tocan el disco.
  9. Lecciones: cada error reparado se guarda en /agent/coder_lessons.txt y se ofrece en los siguientes trabajos.
- tests/test_coder_sim.c (en `make test`): 14 casos de uso con un LLM simulado (perfecto, Python, JavaScript, prosa, manejador de
  ventana, error y reparacion, respuesta cortada, testarudo, modificacion sin cambios, olvida la ventana, desconectado, cancelado,
  revisor que mejora, comillas tipograficas). Los programas de consola resultantes se EJECUTAN y se compara su salida.

## Parche 097 — El programador se usa SIEMPRE y se ve lo que hace
- Fallo real: las misiones a SOMA ("crea una app...") no pasaban por el programador nuevo; el agente generico escribia el archivo y
  compilaba texto del razonamiento del modelo ("check the constraints" -> [COMPILER ERROR L2]).
- Ahora toda mision de programacion (barra de SOMA, terminal, delegacion al agente coder) va a coder_core: deteccion de intencion,
  eleccion del archivo (ruta .soma de la mision, el ultimo programa si se pide modificar, o un nombre sacado de la mision) y al
  terminar se abre en SOMA Studio.
- Extraccion a prueba de modelos con razonamiento: los ``` solo cuentan al principio de linea, se prueban TODOS los bloques (y el
  texto sin bloques) y gana el que mejor verifica; si la respuesta es prosa se detecta ("es TEXTO, no un programa") y se pide el
  codigo. Las preguntas llevan /no_think.
- Visibilidad: cada paso sale en la terminal con [PROGRAMADOR]; la orden `coderlog` resume el ultimo trabajo y `coderlog todo`
  muestra tambien lo que respondio el modelo; todo queda en /agent/coder_ultimo.txt.
- Orden nueva `programa [archivo.soma] <mision>`.
- Simulador: 16 casos (nuevos: razonamiento que escribe ``` dentro del pensamiento, y respuesta solo con razonamiento).

## Parche 098 — Las apps creadas ya no tumban el sistema
- Causa: el Coder ejecutaba la app DENTRO del hilo del escritorio (dispatch_command sin bufer de salida): el bucle del juego
  congelaba todo y la ventana quedaba vacia porque nadie la componia. Ademas, el cerrojo del shell del parche 091 lo retenia la
  orden "vsoma" durante toda la sesion, asi que una app lanzada en segundo plano esperaba para siempre.
- Ahora: las apps se lanzan siempre en su propio hilo; "vsoma"/"gui" no toman el cerrojo; la app dibuja en su superficie y es el
  bucle del escritorio quien abre, compone (cada win.present) y cierra su ventana; las teclas le llegan por su cola.
- La VM cede la CPU cada 4096 instrucciones: un programa sin pausas tampoco congela el escritorio.
- El programador anade sys.sleep(16) a un bucle while (win.active()) que no tenga pausa.
- "Crear con IA" ya no reutiliza calc.soma: el nombre sale de la mision (snake.soma) y nunca pisa un programa existente
  (snake_2.soma...); crear es crear desde cero, no modificar el archivo que hubiera.

## Parche 099 — SOMA Studio: campos de texto de verdad y pestanas rehechas
- src/ui/textfield.c: campo reutilizable (una linea o multilinea con ajuste): cursor, seleccion con raton (clic, arrastre, doble
  clic = palabra, triple = todo, Mayus+clic), Mayus+flechas, Ctrl+A, Ctrl+Retroceso, Inicio/Fin, Supr, UTF-8, portapapeles.
- Lo usan: la descripcion y el archivo de CREAR CON IA, la busqueda y el renombrado de PROYECTOS, la barra SOMA IA del editor y
  las barras Buscar/Reemplazar, Abrir y Guardar como (que selecciona solo el nombre para cambiarlo directamente).
- Clics fiables: cada boton se registra al pintarlo y el clic se comprueba contra lo pintado (antes las zonas eran coordenadas
  fijas que no coincidian con los botones redimensionados por la fuente nueva).
- CREAR CON IA: descripcion multilinea, 8 ideas, archivo automatico (boton Auto), Generar/Cancelar/Probar/Abrir en el editor,
  tarjeta de progreso con pasos numerados, plan, pensamiento en vivo y bitacora, y consejos.
- PROYECTOS: buscador, lista con iconos y ruta, rueda, detalle con Ejecutar / Abrir en el editor / Abrir en Notas / Duplicar,
  Renombrar o mover (editando la ruta) y borrado con confirmacion.
- REGISTRO: bitacora de hasta 40 entradas coloreada, con rueda, Copiar y Traza completa (/agent/coder_ultimo.txt en Notas);
  analisis estatico abajo.

## Parche 100 — Programador: sin panico, sin palabras reservadas, sin "main" olvidado
- KERNEL PANIC (RIP=0xCCCCCCCD, hilo coder_bg): la estructura de entorno del nucleo se creaba sin poner a cero y env.trace quedaba
  con basura de la pila; al llamarla, salto a una direccion invalida. Ahora se inicializa entera (y force_new se pasa de verdad).
- Palabras reservadas como nombre (fn spawn(), let loop...): el normalizador las renombra (spawn_) y la ficha del lenguaje las lista.
- Programas que "terminan con exito" sin hacer nada: si la funcion que contiene el bucle de ventana (o run/start/juego...) no se
  llama nunca, se anade la llamada al final; "main();" se elimina porque fn main() se ejecuta sola y llamarla es un error.
- La verificacion de candidatos ya no llena la terminal de [COMPILER ERROR] (lang_diag_quiet); el detalle sigue en coderlog/REGISTRO.
- Una llamada lenta menos: el plan aparte solo se pide en encargos largos.
- Cerrojo del shell por propietario: el escritorio lo suelta al arrancar sea cual sea la orden que lo abrio (antes, abierto con
  "studio", las apps en segundo plano no arrancaban).

## Parche 101 — Explorador del editor fiable y SOMA-LANG con funciones basicas
- Explorador del editor: el dibujo ponia las filas cada 26 px desde y+34 pero el clic las calculaba cada 22 px desde y+28, asi que a
  partir de la segunda fila el clic caia en otro archivo. Ahora clic y dibujo usan la misma geometria; ademas hay resaltado al pasar
  el raton, rueda para desplazar la lista y la cabecera ya no abre nada al pulsarla.
- SOMA-LANG: nuevas funciones enteras abs(x), min(a,b), max(a,b), clamp(x,lo,hi), sqrt(x); de texto find(s,trozo), chr(n), ord(s,i);
  alias win.key() (= sys.key()) y win.fill()/win.fill_rect() (= win.rect()). Una funcion propia con el mismo nombre manda sobre la del sistema.
- La ficha de lenguaje que recibe el modelo las incluye (y avisa de que no hay decimales ni math.*), para que deje de inventarlas.
- Pruebas: test_lang cubre las funciones nuevas (76 comprobaciones).

## Parche 102 — Fase 1 (seguridad), paso 2: validacion de punteros de usuario comprobable
- Contexto: los pasos 0 y 1 de la Fase 1 (inventario y mapa de superficies) senalaron `usermode.c`, `idt.c`, `apic.c`, `vmm.c`, `shell.c`
  y `kernel.c`. La revision manual de `usermode.c` no encontro un fallo explotable: ya hay comprobacion de desbordamiento, SMAP durante
  las copias y fallos de proceso aislados. Lo que faltaba era una PRUEBA reproducible de esa defensa.
- Nuevo `src/arch/x86_64/uaccess.h`: la validacion de rangos de usuario (ventana, desbordamiento, presente+U+W por pagina) como funcion
  pura. `usermode.c` la usa; un `_Static_assert` impide que sus banderas se desincronicen de `vmm.h`. Ademas se evita la suma `p+len`.
- Nuevo `tests/test_uaccess.c` (en `make test`): casos limite (ultimo byte, justo fuera, direcciones de kernel, 7 desbordamientos de 64
  bits) y 400 000 casos aleatorios contrastados con una referencia en 128 bits, bajo ASan/UBSan.
- `SYS_WRITE` con longitud 0 devuelve 0 en vez de -1.
- Comprobado: `usermode_selftest` completo en QEMU con y sin SMEP/SMAP (`-cpu max`); builds x86_64 y aarch64.

## Parche 102 — Fase 1 (seguridad), paso 2: validacion de punteros de usuario comprobable
- Contexto: los pasos 0 y 1 de la Fase 1 (inventario y mapa de superficies) senalaron `usermode.c`, `idt.c`, `apic.c`, `vmm.c`, `shell.c`
  y `kernel.c`. La revision manual de `usermode.c` no encontro un fallo explotable: ya hay comprobacion de desbordamiento, SMAP durante
  las copias y fallos de proceso aislados. Lo que faltaba era una PRUEBA reproducible de esa defensa.
- Nuevo `src/arch/x86_64/uaccess.h`: la validacion de rangos de usuario (ventana, desbordamiento, presente+U+W por pagina) como funcion
  pura. `usermode.c` la usa; un `_Static_assert` impide que sus banderas se desincronicen de `vmm.h`. Ademas se evita la suma `p+len`.
- Nuevo `tests/test_uaccess.c` (en `make test`): casos limite (ultimo byte, justo fuera, direcciones de kernel, 7 desbordamientos de 64
  bits) y 400 000 casos aleatorios contrastados con una referencia en 128 bits, bajo ASan/UBSan.
- `SYS_WRITE` con longitud 0 devuelve 0 en vez de -1.
- Comprobado: `usermode_selftest` completo en QEMU con y sin SMEP/SMAP (`-cpu max`); builds x86_64 y aarch64.

## Parche 102 — Fase 1 (seguridad), paso 2: validacion de punteros de usuario comprobable
- Contexto: los pasos 0 y 1 de la Fase 1 (inventario y mapa de superficies) senalaron `usermode.c`, `idt.c`, `apic.c`, `vmm.c`, `shell.c`
  y `kernel.c`. La revision manual de `usermode.c` no encontro un fallo explotable: ya hay comprobacion de desbordamiento, SMAP durante
  las copias y fallos de proceso aislados. Lo que faltaba era una PRUEBA reproducible de esa defensa.
- Nuevo `src/arch/x86_64/uaccess.h`: la validacion de rangos de usuario (ventana, desbordamiento, presente+U+W por pagina) como funcion
  pura. `usermode.c` la usa; un `_Static_assert` impide que sus banderas se desincronicen de `vmm.h`. Ademas se evita la suma `p+len`.
- Nuevo `tests/test_uaccess.c` (en `make test`): casos limite (ultimo byte, justo fuera, direcciones de kernel, 7 desbordamientos de 64
  bits) y 400 000 casos aleatorios contrastados con una referencia en 128 bits, bajo ASan/UBSan.
- `SYS_WRITE` con longitud 0 devuelve 0 en vez de -1.
- Comprobado: `usermode_selftest` completo en QEMU con y sin SMEP/SMAP (`-cpu max`); builds x86_64 y aarch64.

## Parche 070: comprobacion de la red `user` en pi5.py

* `qemu-system-aarch64 -netdev help` sin maquina da error ("No machine specified") y `pi5.py` creia que a la imagen le faltaba la red `user`
  aunque estuviera bien construida (con la imagen ya reconstruida se reconstruia en bucle). Ahora se pasa `-M raspi5b`.

## Parche 103 — Fase 1 (seguridad), paso 3: guarda de reentrada del heap
- Revision de `mem.c` (heap): ya tenia canarios, veneno al liberar, deteccion de double free, contabilidad por dueno y 100 pruebas en host.
  Lo unico que no cubria es la concurrencia: no hay cerrojo, porque el nucleo es cooperativo a proposito (parche 072). Eso es seguro mientras
  ninguna IRQ ni punto de desalojo caiga dentro de `kmalloc`/`kfree`; nada lo comprobaba.
- `kmalloc`/`kfree` pasan a envolver a sus implementaciones con una bandera. Una llamada reentrante se rechaza (`kmalloc` -> NULL, `kfree` ->
  se ignora) y se cuenta en `kheap_counters.reentries`, que `kheap_dump_stats` muestra si es > 0. Antes habria corrompido las listas en silencio.
- `kheap_test_reentry()` + `test_reentrada` en `tests/test_heap.c` (98 -> 102 comprobaciones, bajo ASan/UBSan). Sin cambios de comportamiento normal.

## Parche 104 — Fase 1 (seguridad), paso 4: espacios de direcciones de usuario probados en host
- Revision de `vmm.c` y de las rutas de `usermode.c` que lo usan: `vmm_as_map` solo permite la entrada PML4[1] del proceso, las tablas del kernel
  se comparten por valor y `vmm_as_destroy` libera PT/PD/PDPT/PML4 sin tocar los marcos de datos (los libera el dueno). No hay fallo explotable.
- Hueco cerrado: `vmm_as_destroy` liberaba las tablas aunque fueran las del espacio ACTIVO (CR3 habria quedado apuntando a memoria reutilizable).
  Hoy `usermode.c` nunca lo hace, pero nada lo impedia a un llamador futuro: ahora vuelve antes al espacio del kernel.
- `vmm.c` se puede compilar en host con `-DVMM_HOST_TEST` (solo sustituye CR3/INVLPG/CR0; el kernel se compila igual que antes).
- Nuevo `tests/test_vmm.c` (en `make test`, bajo ASan/UBSan): 50 ciclos crear/mapear 700 paginas/destruir sin fugas de marcos, rechazo de
  mapeos fuera de la region de usuario, PML4 del kernel intacto, el PMM fallando en cada uno de los 12 primeros marcos (sin fugas ni dobles
  liberaciones) y destruccion del espacio activo. Verificado que el test falla si se quita la guarda.

## Parche 105 — Correccion del test de vmm en macOS
- `tests/test_vmm.c` (parche 104) fallaba al enlazar en macOS/arm64 (`___data_start` sin definir): glibc aporta `__data_start`, macOS no.
  Con `-DVMM_HOST_TEST`, `vmm.c` renombra los simbolos del enlazador (`__host_*`) y el test define los suyos: vale igual en Linux y en macOS.
  Sin cambios en el kernel (el renombrado solo existe en la prueba en host).

## Parche 106 — `make test` completo en macOS (terminal)
- `tests/test_termcore.c` fallaba al enlazar en macOS (`_clip_seq`, `_clip_set_text`, `_clip_text`): `term_core.c` declara el portapapeles del
  sistema como simbolos debiles sin definir (en Linux valen NULL), y el enlazador de Mach-O no lo admite en un ejecutable.
- El test define esas tres funciones solo en macOS (sin portapapeles compartido, mismo comportamiento que NULL). El kernel no cambia.
- Auditoria: ningun otro binario de prueba tiene simbolos debiles sin definir (comprobado con `nm`).

## Parche 107 — SRCFS: el indice de fuentes ya cabe todo `src/` (fallo real hallado por `make test`)
- Sintoma: en macOS `test_srcfs` fallaba (`src_ls net/` no listaba nada). No era de macOS: reproducido en Linux. El indice de fuentes admitia 255
  entradas y `src/` ya tiene mas de 320 archivos; el empaquetado cortaba por ORDEN ALFABETICO, asi que `net/`, `lang/`, `ui/` y `user/`
  no estaban en la imagen. El agente de SOMA no podia leer con `src_ls`/`src_cat`/`src_grep` ni el codigo de red, ni el del lenguaje, ni la GUI.
- Correccion: indice de 64 sectores (511 entradas) en `srcfs.c`, `disk_layout.h` y `ejecutar.py` (los datos empiezan ahora en el LBA 8256).
  `ejecutar.py` reempaqueta en cada arranque, asi que el disco se actualiza solo.
- `tests/test_srcfs_pack.py`: capacidad 511 y una comprobacion nueva que falla si alguna carpeta de `src/` queda fuera de la imagen.
- Limite honesto: con 511 entradas quedan ~190 de margen. Si `src/` los supera, el empaquetado avisa (`[SRCFS] AVISO`).

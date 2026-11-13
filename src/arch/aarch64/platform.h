#ifndef SOMA_PLATFORM_H
#define SOMA_PLATFORM_H
/* PARCHE 058: constantes de cada plataforma aarch64. PLATFORM=qemu_virt (por defecto) o PLATFORM=rpi5 (Raspberry Pi 5, BCM2712). */
#if defined(PLATFORM_rpi5)
#define SOMA_PLATFORM_NAME   "Raspberry Pi 5 (BCM2712)"
#define SOMA_UART_BASE       0x107d001000ULL   /* UART de depuracion PL011 (conector de 3 pines), la inicializa el firmware */
#define SOMA_PMM_BASE        0x20000000UL      /* reserva de marcos: RAM de la placa desde 512 MiB (todas las Pi 5 tienen >= 2 GiB) */
#define SOMA_HAVE_VIRTIO     0
#define SOMA_SD_BASE         0x1000fff000ULL   /* SDHCI de la ranura de tarjeta (SDIO1) */
#define SOMA_SDIO2_BASE      0x1001100000ULL   /* PARCHE 079: SDHCI del Wi-Fi (SDIO2, chip CYW43455) */
#define SOMA_GIO_BASE        0x107d508500ULL   /* PARCHE 079: GPIO de la BCM2712 (brcmstb): WL_ON = pin 28 del banco 0 */
#define SOMA_PINCTRL_BASE    0x107d504100ULL   /* PARCHE 079: funciones de los pines de la BCM2712 (SD2 = pines 30-35) */
#define SOMA_MBOX_BASE       0x107c013880ULL   /* PARCHE 063: mailbox del VideoCore (canal 8 = propiedades): lectura +0, estado +0x18, escritura +0x20 */
#define SOMA_PCIE_BASE       0x1000110000ULL   /* PARCHE 062: root complex PCIe1 (conector externo), tipo Broadcom STB */
#define SOMA_PCIE_MEM_CPU    0x1800000000ULL   /* ventana de PCI memoria vista por la CPU (apertura de 16 GiB de PCIe1) */
#define SOMA_PCIE_MEM_SIZE   0x40000000ULL     /* ... de la que se usa 1 GiB: PCI 0 .. 1 GiB */
#define SOMA_PCIE2_BASE      0x1000120000ULL   /* PARCHE 067: root complex PCIe2, el enlace del RP1 (mismo tipo que PCIe1) */
#define SOMA_PCIE2_MEM_CPU   0x1F00000000ULL   /* ventana de PCI memoria de PCIe2 vista por la CPU (como la deja el firmware con pciex4_reset=0) */
#define SOMA_PCIE_DMA_PCI    0x1000000000ULL   /* direccion PCI por la que los dispositivos ven la RAM (como Linux en la Pi 5) */
#define SOMA_THREAD_STACK_BASE 0x10000000ULL   /* 1024 ranuras de 64 KiB: 0x10000000-0x14000000 */
#define SOMA_DMA_POOL_BASE   0x1E000000UL      /* PARCHE 071: 4 MiB para DMA (xHCI, GEM), entre el heap y la reserva de marcos; mapeados como memoria normal NO cacheable */
#define SOMA_DMA_POOL_SIZE   0x400000UL
#define SOMA_HEAP_START      0x18000000ULL     /* heap del kernel (hasta 64 MiB) por debajo de la reserva de marcos */
#define SOMA_GICD_BASE       0x107fff9000ULL   /* GIC-400: distribuidor y interfaz de CPU */
#define SOMA_GICC_BASE       0x107fffa000ULL
#define SOMA_MMU_NORMAL_GIB_FIRST 0            /* bloques de 1 GiB con memoria normal (RAM): 0..0 */
#define SOMA_MMU_NORMAL_GIB_LAST  0
#define SOMA_MMU_DEVICE_GIB_FIRST 64           /* ventana de perifericos y PCIe de la BCM2712: 64..127 GiB */
#define SOMA_MMU_DEVICE_GIB_LAST  127
#else
#define SOMA_PLATFORM_NAME   "QEMU virt"
#define SOMA_UART_BASE       0x09000000ULL
#define SOMA_PMM_BASE        0x44000000UL
#define SOMA_HAVE_VIRTIO     1
#define SOMA_SD_BASE         0                 /* QEMU virt no tiene SDHCI */
#define SOMA_SDIO2_BASE      0                 /* sin Wi-Fi */
#define SOMA_GIO_BASE        0
#define SOMA_PINCTRL_BASE    0
#define SOMA_MBOX_BASE       0                 /* sin VideoCore */
#define SOMA_PCIE_BASE       0                 /* sin root complex Broadcom */
#define SOMA_PCIE_MEM_CPU    0
#define SOMA_PCIE2_BASE      0
#define SOMA_PCIE2_MEM_CPU   0
#define SOMA_PCIE_MEM_SIZE   0
#define SOMA_PCIE_DMA_PCI    0
#define SOMA_THREAD_STACK_BASE 0x43000000ULL
#define SOMA_DMA_POOL_BASE   0
#define SOMA_DMA_POOL_SIZE   0
#define SOMA_HEAP_START      0x41000000ULL
#define SOMA_GICD_BASE       0x08000000ULL
#define SOMA_GICC_BASE       0x08010000ULL
#define SOMA_MMU_NORMAL_GIB_FIRST 1            /* RAM de QEMU virt: 1..1 GiB (hasta 2 GiB) */
#define SOMA_MMU_NORMAL_GIB_LAST  1
#define SOMA_MMU_DEVICE_GIB_FIRST 0            /* GIC, UART, virtio y ECAM: primer GiB */
#define SOMA_MMU_DEVICE_GIB_LAST  0
#endif
#endif

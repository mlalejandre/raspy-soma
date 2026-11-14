ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
	CC = gcc
	LD = ld
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -m64 -march=x86-64 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-pic -mno-red-zone -mgeneral-regs-only -fno-omit-frame-pointer
else
	CC = aarch64-linux-gnu-gcc
	LD = aarch64-linux-gnu-ld
	OBJCOPY_A64 = aarch64-linux-gnu-objcopy
	PLATFORM ?= qemu_virt
CFLAGS = -DPLATFORM_$(PLATFORM) -march=armv8-a -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fno-pie -fno-pic -mgeneral-regs-only -mstrict-align -mno-outline-atomics -fno-omit-frame-pointer
endif

BUILD = build/$(ARCH)
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS += -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/$(ARCH)

OBJS_ARCH = \
	$(BUILD)/boot.o \
	$(BUILD)/switch.o \
	$(BUILD)/idt.o \
	$(BUILD)/pci.o \
	$(BUILD)/rtc.o \
	$(BUILD)/sysinfo.o \
	$(BUILD)/pmm.o \
	$(BUILD)/vmm.o \
	$(BUILD)/usermode.o \
	$(BUILD)/hda.o \
	$(BUILD)/smp.o \
	$(BUILD)/hwmon.o

ifeq ($(ARCH),aarch64)
OBJS_ARCH += $(BUILD)/rpi_fb.o $(BUILD)/mmu.o $(BUILD)/gic.o $(BUILD)/sdhci.o $(BUILD)/blk_glue.o $(BUILD)/pcie.o $(BUILD)/rp1.o $(BUILD)/rp1_gpio.o $(BUILD)/gem.o $(BUILD)/xhci.o $(BUILD)/xhci2.o $(BUILD)/wifi.o      # PARCHE 060 / 061 / 062 / 077 / 079
endif
ifeq ($(ARCH),x86_64)
OBJS_ARCH += $(BUILD)/apic.o $(BUILD)/ahci.o $(BUILD)/nvme.o $(BUILD)/blk_glue.o $(BUILD)/xhci.o      # PARCHE 037 / 040 / 043 / 045 / 046
endif

OBJS_CORE = \
	$(BUILD)/thread.o \
	$(BUILD)/mutex.o \
	$(BUILD)/panic.o \
	$(BUILD)/kernel.o \
	$(BUILD)/mem.o \
	$(BUILD)/ramstat.o \
	$(BUILD)/console.o \
	$(BUILD)/virtio_net.o \
	$(BUILD)/net.o \
	$(BUILD)/netif.o \
	$(BUILD)/dhcp.o \
	$(BUILD)/dhcp_proto.o \
	$(BUILD)/ip.o \
	$(BUILD)/udp.o \
	$(BUILD)/dns.o \
	$(BUILD)/tcp.o \
	$(BUILD)/http.o \
	$(BUILD)/llm.o \
	$(BUILD)/fs.o \
	$(BUILD)/virtio_blk.o \
	$(BUILD)/srcfs.o \
	$(BUILD)/bigfs.o \
	$(BUILD)/bigfs_glue.o \
	$(BUILD)/boot_gate.o \
	$(BUILD)/nano.o \
	$(BUILD)/agent.o \
	$(BUILD)/shell.o \
	$(BUILD)/httpd.o \
	$(BUILD)/fb.o \
	$(BUILD)/gui_font.o \
	$(BUILD)/mouse.o \
	$(BUILD)/gui.o \
	$(BUILD)/browser.o \
	$(BUILD)/image.o \
	$(BUILD)/imgview.o \
	$(BUILD)/fm.o \
	$(BUILD)/notepad.o \
	$(BUILD)/memview.o $(BUILD)/js_runtime.o \
	$(BUILD)/layout.o $(BUILD)/tex.o $(BUILD)/settings.o $(BUILD)/savedlg.o $(BUILD)/codeedit.o $(BUILD)/textfield.o $(BUILD)/coder_core.o $(BUILD)/coder_demo.o $(BUILD)/pdf.o $(BUILD)/pdfview.o $(BUILD)/sysmon.o \
	$(BUILD)/paint.o \
	$(BUILD)/coder_app.o \
	$(BUILD)/vm.o \
	$(BUILD)/lexer.o \
	$(BUILD)/lang.o

.DEFAULT_GOAL := all

# SOMA_PARCHE_016_EXEC: programas de usuario (src/user/*.c) -> ELF -> objeto enlazado en el kernel (solo x86_64)
ifeq ($(ARCH),x86_64)
USER_PROGS = hello fault ticker sumfile wavplay ssetest fbtest sysfuzz      # se enlazan en el kernel y se copian a /bin/ (caben en el VFS de 16 KiB)
USER_PROGS_BIG = bigprog vidplay                    # SOMA_PARCHE_019_FILES: demasiado grandes para el VFS; se construyen aparte y viven en el almacen BIGFS
USER_CFLAGS = -m64 -march=x86-64 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -fPIE -mno-red-zone -mgeneral-regs-only -nostdlib -static -Isrc/user
OBJS_ARCH += $(BUILD)/userprogs.o

# SOMA_PARCHE_021_FPU: ssetest usa SSE2/x87: sin -mgeneral-regs-only (el resto de programas sigue sin SSE)
$(BUILD)/user/ssetest.elf: USER_CFLAGS := $(filter-out -mgeneral-regs-only,$(USER_CFLAGS)) -msse2 -mfpmath=sse -fno-tree-vectorize

$(BUILD)/user/%.elf: src/user/%.c src/user/ulib.h src/user/user.ld $(wildcard src/core/wav.*) $(wildcard src/core/jpeg.*) $(wildcard src/core/avi.*) | $(BUILD)
	mkdir -p $(BUILD)/user
	$(CC) $(USER_CFLAGS) -Wl,-T,src/user/user.ld -Wl,-z,max-page-size=4096 $< -o $@

all: $(foreach p,$(USER_PROGS_BIG),$(BUILD)/user/$(p).elf)

$(BUILD)/userprogs.o: $(foreach p,$(USER_PROGS),$(BUILD)/user/$(p).elf)
	cd $(BUILD)/user && $(LD) -r -b binary -o ../userprogs.o $(foreach p,$(USER_PROGS),$(p).elf)
endif

OBJS_CORE += $(BUILD)/elf.o $(BUILD)/audio.o $(BUILD)/wav.o $(BUILD)/audio_sys.o
# SOMA_PARCHE_023_MEDIA: reproductor multimedia de la GUI
OBJS_CORE += $(BUILD)/jpeg.o $(BUILD)/avi.o $(BUILD)/media.o
# SOMA_PARCHE_024_TERM: emulador de terminal de la GUI
OBJS_CORE += $(BUILD)/term_core.o $(BUILD)/term_ui.o
# SOMA_PARCHE_PORTAPAPELES: portapapeles global del sistema
OBJS_CORE += $(BUILD)/clipboard.o
# SOMA_PARCHE_NAVEGADOR: motor web (HTML5/CSS3/JavaScript) para el navegador
OBJS_CORE += $(BUILD)/web_regex.o $(BUILD)/web_js_regex.o $(BUILD)/web_js_run.o $(BUILD)/web_js_lib.o $(BUILD)/web_js_lib2.o $(BUILD)/web_js_lib3.o $(BUILD)/web_js_lib4.o $(BUILD)/web_js_parse.o $(BUILD)/web_js_api.o $(BUILD)/web_js_dom.o $(BUILD)/web_f64.o $(BUILD)/web_net.o $(BUILD)/web_html.o $(BUILD)/web_css.o $(BUILD)/web_layout.o $(BUILD)/web_font.o $(BUILD)/web_img.o $(BUILD)/web_jpeg.o $(BUILD)/web_inflate.o $(BUILD)/web_page.o
$(BUILD)/web_font.o: src/apps/web_dejavu.ttf

# PARCHE 030: protector de pila opcional (make SSP=1). Detecta desbordamientos que pisan la direccion de retorno y
# detiene el sistema con un mensaje en lugar de seguir con la pila corrupta.
# (activado por defecto en x86_64; make SSP=0 lo desactiva. En aarch64 sigue desactivado hasta poder probarlo.)
ifeq ($(ARCH),x86_64)
SSP ?= 1
else
SSP ?= 0
endif
ifeq ($(SSP),1)
CFLAGS += -fstack-protector-strong -mstack-protector-guard=global
OBJS_CORE += $(BUILD)/ssp.o
endif

# PARCHE 032: descripcion del hardware (ACPI / FDT)
OBJS_CORE += $(BUILD)/hwinfo.o $(BUILD)/nvme_proto.o $(BUILD)/blk.o $(BUILD)/xhci_proto.o $(BUILD)/hid_proto.o $(BUILD)/msc_proto.o $(BUILD)/fat.o $(BUILD)/sd_proto.o $(BUILD)/pcie_proto.o $(BUILD)/mbox_proto.o $(BUILD)/rp1_proto.o $(BUILD)/gem_proto.o $(BUILD)/dma_pool.o $(BUILD)/gpio_proto.o $(BUILD)/sdio_proto.o $(BUILD)/crypto.o $(BUILD)/wpa.o $(BUILD)/brcm_proto.o $(BUILD)/png_enc.o $(BUILD)/conc_test.o

OBJS = $(OBJS_ARCH) $(OBJS_CORE)
HEADERS = $(wildcard src/*.h) $(wildcard src/*/*.h) $(wildcard src/cursors/*.h) $(wildcard src/fonts/*.h) $(wildcard src/icons/*.h) $(wildcard src/arch/$(ARCH)/*.h)

all: $(BUILD)/soma.iso

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.o: src/arch/$(ARCH)/boot.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/switch.o: src/arch/$(ARCH)/switch.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/arch/$(ARCH)/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# PARCHE 062: el controlador xHCI es el mismo en x86_64 y aarch64 (vive en src/arch/x86_64; lo propio de cada arquitectura va en xhci_arch.h)
ifeq ($(ARCH),aarch64)
$(BUILD)/xhci.o: src/arch/x86_64/xhci.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
endif

$(BUILD)/%.o: src/core/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/drivers/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/fs/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/net/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/ui/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/apps/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/lang/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/agent/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

ifeq ($(ARCH)-$(PLATFORM),aarch64-rpi5)
LDEXTRA = --defsym=SOMA_BASE=0x80000
endif
$(BUILD)/soma.elf: $(OBJS) linker_$(ARCH).ld
	$(LD) $(LDEXTRA) -T linker_$(ARCH).ld -o $@ $(OBJS)
ifeq ($(ARCH)-$(PLATFORM),aarch64-rpi5)
	$(OBJCOPY_A64) -O binary $@ $(BUILD)/kernel_2712.img          # PARCHE 058: imagen para la tarjeta SD de la Pi 5 (se carga en 0x80000)
endif

$(BUILD)/soma.iso: $(BUILD)/soma.elf
ifeq ($(ARCH),x86_64)
	mkdir -p $(BUILD)/isodir/boot/grub
	cp $(BUILD)/soma.elf $(BUILD)/isodir/boot/soma.elf
	cp grub/grub.cfg $(BUILD)/isodir/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/isodir
	grub-file --is-x86-multiboot $(BUILD)/soma.elf
	cp $@ build/soma.iso
	cp $(BUILD)/soma.elf build/soma.elf
else
	@echo "ISO not applicable for aarch64. Target is bare ELF."
	touch $@
	cp $(BUILD)/soma.elf build/soma.elf
endif

clean:
	rm -rf build

# SOMA_PARCHE_002_HOST_TESTS: pruebas en host (no afectan a la build del kernel)
HOST_CC ?= cc
TEST_INC = -Itests -Itests/host_inc -Isrc -Isrc/core -Isrc/drivers -Isrc/fs -Isrc/net -Isrc/ui -Isrc/apps -Isrc/agent -Isrc/lang -Isrc/arch/x86_64
TEST_WARN = -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-address -Wno-unused-function -Wno-unused-variable
TEST_CFLAGS = -std=gnu11 $(TEST_WARN) -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer $(TEST_INC) $(TEST_EXTRA)
ifeq ($(STRICT),1)
TEST_CFLAGS += -Werror
endif

# PARCHE 032: pruebas y fuzzing (ASAN+UBSAN) de los analizadores ACPI/FDT
fuzz-nvme:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_nvme.c src/core/nvme_proto.c -o build/fuzz/test_nvme
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_nvme

fuzz-xhci:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_xhci.c src/core/xhci_proto.c -o build/fuzz/test_xhci
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_xhci

fuzz-hid:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_hid.c src/core/hid_proto.c src/core/xhci_proto.c -o build/fuzz/test_hid
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_hid

fuzz-msc:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_msc.c src/core/msc_proto.c -o build/fuzz/test_msc
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_msc

fuzz-fat:
	@mkdir -p build/fuzz/fat
	python3 tools/mkfat.py build/fuzz/fat
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_fat.c src/core/fat.c -o build/fuzz/test_fat
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_fat build/fuzz/fat 6000

fuzz-hw:
	@mkdir -p build/fuzz
	$(HOST_CC) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined tests/test_hwinfo.c src/core/hwinfo.c -o build/fuzz/test_hwinfo
	./build/fuzz/test_hwinfo 50000

test:
	@mkdir -p build/tests
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lexer.c src/lang/lexer.c -o build/tests/test_lexer
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sbuf.c -o build/tests/test_sbuf
	$(HOST_CC) $(TEST_CFLAGS) tests/test_lang.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_lang
	$(HOST_CC) $(TEST_CFLAGS) tests/test_coder_sim.c src/apps/coder_core.c tests/stubs_common.c tests/stubs_lang.c src/lang/lang.c src/lang/vm.c src/lang/lexer.c -o build/tests/test_coder_sim
	$(HOST_CC) $(TEST_CFLAGS) tests/test_fs.c tests/stubs_common.c tests/stubs_fs.c src/fs/fs.c -o build/tests/test_fs
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_heap_cfg.h tests/test_heap.c tests/stubs_common.c tests/stubs_heap.c tests/stubs_vmm.c src/core/mem.c -o build/tests/test_heap
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_sched_cfg.h tests/test_sched.c src/core/thread.c src/core/panic.c src/core/mutex.c tests/stubs_common.c tests/stubs_vmm.c tests/stubs_sched.c -o build/tests/test_sched
	$(HOST_CC) $(TEST_CFLAGS) -DSOMA_HOST_PANIC=1 tests/test_panic.c src/core/panic.c tests/stubs_common.c tests/stubs_vmm.c -o build/tests/test_panic
	$(HOST_CC) $(TEST_CFLAGS) -include tests/host_sched_cfg.h tests/test_mutex.c src/core/mutex.c src/core/thread.c src/core/panic.c tests/stubs_common.c tests/stubs_vmm.c tests/stubs_sched.c -o build/tests/test_mutex
	$(HOST_CC) $(TEST_CFLAGS) tests/test_srcfs.c src/fs/srcfs.c tests/stubs_common.c tests/stubs_fs.c -o build/tests/test_srcfs
	$(HOST_CC) $(TEST_CFLAGS) tests/test_elf.c src/core/elf.c -o build/tests/test_elf
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/arch/x86_64 tests/test_uaccess.c -o build/tests/test_uaccess
	$(HOST_CC) $(TEST_CFLAGS) -Isrc/arch/x86_64 tests/test_pic_irq.c -o build/tests/test_pic_irq
	$(HOST_CC) $(TEST_CFLAGS) tests/test_agent_policy.c -o build/tests/test_agent_policy
	$(HOST_CC) $(TEST_CFLAGS) tests/test_slotio.c -o build/tests/test_slotio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_ckptio.c -o build/tests/test_ckptio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_spillio.c src/fs/bigfs.c -o build/tests/test_spillio
	$(HOST_CC) $(TEST_CFLAGS) -DVMM_HOST_TEST tests/test_vmm.c tests/stubs_common.c src/arch/x86_64/vmm.c -o build/tests/test_vmm
	$(HOST_CC) $(TEST_CFLAGS) tests/test_bigfs.c src/fs/bigfs.c -o build/tests/test_bigfs
	$(HOST_CC) $(TEST_CFLAGS) tests/test_audio.c src/core/audio.c -o build/tests/test_audio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_wav.c src/core/wav.c -o build/tests/test_wav
	$(HOST_CC) $(TEST_CFLAGS) tests/test_jpeg.c src/core/jpeg.c -o build/tests/test_jpeg
	$(HOST_CC) $(TEST_CFLAGS) tests/test_avi.c src/core/avi.c -o build/tests/test_avi
	$(HOST_CC) $(TEST_CFLAGS) tests/test_termcore.c src/apps/term_core.c -o build/tests/test_termcore
	./build/tests/test_lexer
	./build/tests/test_sbuf
	./build/tests/test_lang
	./build/tests/test_coder_sim
	./build/tests/test_fs
	./build/tests/test_fs persist-write build/tests/disk.img
	./build/tests/test_fs persist-check build/tests/disk.img
	./build/tests/test_heap
	$(HOST_CC) $(TEST_CFLAGS) tests/test_hwinfo.c src/core/hwinfo.c -o build/tests/test_hwinfo
	./build/tests/test_hwinfo 2000
	$(HOST_CC) $(TEST_CFLAGS) tests/test_nvme.c src/core/nvme_proto.c -o build/tests/test_nvme
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_nvme
	$(HOST_CC) $(TEST_CFLAGS) tests/test_blk.c src/core/blk.c -o build/tests/test_blk
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_blk
	$(HOST_CC) $(TEST_CFLAGS) tests/test_xhci.c src/core/xhci_proto.c -o build/tests/test_xhci
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_xhci
	$(HOST_CC) $(TEST_CFLAGS) tests/test_hid.c src/core/hid_proto.c src/core/xhci_proto.c -o build/tests/test_hid
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_hid
	$(HOST_CC) $(TEST_CFLAGS) tests/test_msc.c src/core/msc_proto.c -o build/tests/test_msc
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_msc
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sd.c src/core/sd_proto.c -o build/tests/test_sd
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_sd
	$(HOST_CC) $(TEST_CFLAGS) tests/test_pcie.c src/core/pcie_proto.c -o build/tests/test_pcie
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_pcie
	$(HOST_CC) $(TEST_CFLAGS) tests/test_mbox.c src/core/mbox_proto.c -o build/tests/test_mbox
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_mbox
	$(HOST_CC) $(TEST_CFLAGS) tests/test_rp1.c src/core/rp1_proto.c -o build/tests/test_rp1
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_rp1
	$(HOST_CC) $(TEST_CFLAGS) tests/test_gem.c src/core/gem_proto.c -o build/tests/test_gem
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_gem
	$(HOST_CC) $(TEST_CFLAGS) tests/test_gpio.c src/core/gpio_proto.c -o build/tests/test_gpio
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_gpio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_sdio.c src/core/sdio_proto.c -o build/tests/test_sdio
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_sdio
	$(HOST_CC) $(TEST_CFLAGS) tests/test_wpa.c src/core/crypto.c src/core/wpa.c src/net/netif.c -o build/tests/test_wpa
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_wpa
	$(HOST_CC) $(TEST_CFLAGS) tests/test_brcm.c src/core/brcm_proto.c src/net/dhcp_proto.c -o build/tests/test_brcm
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_brcm
	$(HOST_CC) $(TEST_CFLAGS) tests/test_png.c src/core/png_enc.c -o build/tests/test_png
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_png
	python3 tests/check_png.py build/tests
	$(HOST_CC) $(TEST_CFLAGS) tests/test_tex.c src/apps/tex.c -o build/tests/test_tex
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_tex
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_pdf.c src/apps/pdf.c src/apps/web_inflate.c src/apps/web_img.c src/apps/web_jpeg.c src/core/jpeg.c -o build/tests/test_pdf
	python3 tests/pdf_seeds.py build/tests/pdf
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_pdf --check build/tests/pdf/clasico.pdf build/tests/pdf/xrefstm.pdf
	$(HOST_CC) $(TEST_CFLAGS) tests/test_dma_pool.c src/core/dma_pool.c -o build/tests/test_dma_pool
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_dma_pool
	@mkdir -p build/tests/fat
	python3 tools/mkfat.py build/tests/fat
	$(HOST_CC) $(TEST_CFLAGS) tests/test_fat.c src/core/fat.c -o build/tests/test_fat
	ASAN_OPTIONS=detect_leaks=0 ./build/tests/test_fat build/tests/fat 800
	./build/tests/test_sched
	./build/tests/test_panic
	./build/tests/test_mutex
	./build/tests/test_uaccess
	./build/tests/test_pic_irq
	./build/tests/test_agent_policy
	./build/tests/test_slotio
	./build/tests/test_ckptio
	./build/tests/test_spillio
	./build/tests/test_vmm
	./build/tests/test_elf $(wildcard build/x86_64/user/*.elf)
	./build/tests/test_bigfs
	python3 tests/test_bigfs_xcheck.py build/tests/test_bigfs
	./build/tests/test_audio
	./build/tests/test_wav
	python3 tests/test_video.py
	./build/tests/test_termcore
	python3 tests/test_wavcheck.py
	python3 tests/test_fpcheck.py
	python3 tests/test_srcfs_pack.py
	python3 tests/test_srcfs_pack.py --emit build/tests/srcfs.img build/tests/srcfs.manifest
	./build/tests/test_srcfs build/tests/srcfs.img build/tests/srcfs.manifest
	@echo "TESTS OK"

audit:
	python3 tools/audit.py

# PARCHE 031: fuzzing de la pila de red (ARP/IP/ICMP/UDP/TCP) con ASAN+UBSAN
fuzz-net:
	@mkdir -p build/fuzz
	$(HOST_CC) -std=gnu11 -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined $(TEST_INC) tests/fuzz_net.c src/net/net.c src/net/ip.c src/net/udp.c src/net/tcp.c -o build/fuzz/fuzz_net
	./build/fuzz/fuzz_net $(or $(FUZZ_ITERS),20000) $(or $(FUZZ_SEED),1)
	@echo "FUZZ-NET OK"

# PARCHE 088: fuzzing del lector de PDF
fuzz-pdf:
	@mkdir -p build/fuzz
	python3 tests/pdf_seeds.py build/fuzz/pdf
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 tests/test_pdf.c src/apps/pdf.c src/apps/web_inflate.c src/apps/web_img.c src/apps/web_jpeg.c src/core/jpeg.c -o build/fuzz/test_pdf
	ASAN_OPTIONS=detect_leaks=0 ./build/fuzz/test_pdf --fuzz $(or $(FUZZ_ITERS),3000) $(or $(FUZZ_SEED),1) build/fuzz/pdf/*.pdf

# PARCHE 029: fuzzing de decodificadores con ASAN+UBSAN (necesita Pillow para crear las semillas)
fuzz:
	@mkdir -p build/fuzz
	python3 tests/fuzz_seeds.py build/fuzz/seeds
	$(HOST_CC) -O1 -g -w -fsanitize=address,undefined -fno-sanitize-recover=undefined -DWEB_HOST_TEST -Isrc -Isrc/core -Isrc/apps -Isrc/arch/x86_64 -Isrc/drivers -Isrc/fs -Isrc/lang tests/fuzz_decoders.c src/apps/web_img.c src/apps/web_jpeg.c src/apps/web_inflate.c src/core/jpeg.c -o build/fuzz/fuzz_decoders
	./build/fuzz/fuzz_decoders $(or $(FUZZ_ITERS),2000) $(or $(FUZZ_SEED),1) build/fuzz/seeds/*
	@echo "FUZZ OK"

# SOMA_PARCHE_004_CI: lo que ejecuta la integracion continua
ci:
	$(MAKE) test STRICT=1
	python3 tools/audit.py --strict

# Requiere la imagen compilada (make ARCH=...) y QEMU instalado
boot-smoke:
	python3 tools/boot_smoke.py --arch $(ARCH) $(if $(filter x86_64,$(ARCH)),--max-seconds 6)

# Falla si el SO vuelve a consumir CPU en reposo (solo Linux + QEMU, x86_64)
idle-check:
	python3 tools/idle_check.py

# Provoca un panic y un cuelgue en QEMU y comprueba backtrace + vigilante (x86_64)
fault-check:
	python3 tools/fault_check.py

.PHONY: all clean test fuzz fuzz-net fuzz-hw fuzz-nvme fuzz-xhci fuzz-hid fuzz-msc fuzz-fat audit ci boot-smoke idle-check fault-check

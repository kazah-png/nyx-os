# ============================================================
# kernel/Makefile - Compilación del kernel NyxOS x86_64
# ============================================================

CC = gcc
AS = nasm
LD = ld

CFLAGS = -std=gnu99 -ffreestanding -Os -Wall -Wextra \
         -nostdlib -nostartfiles -nodefaultlibs \
         -fno-stack-protector -fno-pie -m64 \
         -mno-red-zone -mno-sse -mno-mmx -mno-sse2 \
         -mno-3dnow -mcmodel=large \
         -ffunction-sections -fdata-sections \
         -I.



ASFLAGS = -f elf64

LDFLAGS = -T linker.ld -nostdlib -m elf_x86_64

OBJS_KERNEL = boot.o kernel.o gdt.o idt.o isr.o isr_stubs.o irq.o \
        memory.o heap.o slab.o process.o syscall.o pipe.o vfs.o ext2.o ata.o net.o udp.o \
        ip.o arp.o ethernet.o rtl8139.o icmp.o timer.o keyboard.o screen.o \
        serial.o umoddi3.o paging.o switch.o stubs.o dhcp.o \
        vbe.o fb.o mouse.o gui.o font.o compositor.o speaker.o tcp.o \
        sb16.o terminal_win.o fileman_win.o paint_win.o taskman_win.o \
        editor_win.o imageview_win.o soundtest_win.o calc_win.o \
        elf.o initramfs.o rtc.o apic.o dns.o http.o smp.o trampoline.o bootsplash.o auth.o login.o sha256.o

OBJS = $(OBJS_KERNEL)

all: $(USER_ELFS) nyx-kernel.bin

nyx-kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# User-space programs (x86_64 native)
USER_DIR = ../user
USER_ELFS = $(USER_DIR)/hello.elf $(USER_DIR)/init.elf $(USER_DIR)/spin.elf $(USER_DIR)/fdleak.elf $(USER_DIR)/hello_nyx.elf $(USER_DIR)/args.elf \
            $(USER_DIR)/sh.elf $(USER_DIR)/echo.elf $(USER_DIR)/upper.elf $(USER_DIR)/cat.elf $(USER_DIR)/wc.elf $(USER_DIR)/ls.elf

# Flags for Nyx C output (GCC: statement-exprs, __auto_type). -mno-red-zone is mandatory.
NYXCFLAGS = -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR)

$(USER_DIR)/hello.elf: $(USER_DIR)/hello.asm
	nasm -f elf64 $< -o $(USER_DIR)/hello.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/hello.o

$(USER_DIR)/spin.elf: $(USER_DIR)/spin.asm
	nasm -f elf64 $< -o $(USER_DIR)/spin.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/spin.o

$(USER_DIR)/fdleak.elf: $(USER_DIR)/fdleak.asm
	nasm -f elf64 $< -o $(USER_DIR)/fdleak.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/fdleak.o

$(USER_DIR)/init.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o

$(USER_DIR)/args.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/args.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/args.o

$(USER_DIR)/args.o: $(USER_DIR)/args.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# The userspace shell + its coreutils (crt0 + libc + program)
$(USER_DIR)/sh.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/sh.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/sh.o

$(USER_DIR)/echo.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/echo.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/echo.o

$(USER_DIR)/upper.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/upper.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/upper.o

$(USER_DIR)/cat.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/cat.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/cat.o

$(USER_DIR)/wc.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/wc.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/wc.o

$(USER_DIR)/ls.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/ls.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/ls.o

$(USER_DIR)/sh.o: $(USER_DIR)/sh.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/echo.o: $(USER_DIR)/echo.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/upper.o: $(USER_DIR)/upper.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/cat.o: $(USER_DIR)/cat.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/wc.o: $(USER_DIR)/wc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/ls.o: $(USER_DIR)/ls.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/crt0.o: $(USER_DIR)/crt0.asm
	nasm -f elf64 $< -o $@

# Nyx C "hello": crt0 + runtime + transpiled output. Reuses the existing crt0
# (it calls main / exit). hello_nyx.c is nyxc's C output from hello.nyx.
$(USER_DIR)/hello_nyx.elf: $(USER_DIR)/crt0.o $(USER_DIR)/nyxrt.o $(USER_DIR)/hello_nyx.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ \
	    $(USER_DIR)/crt0.o $(USER_DIR)/nyxrt.o $(USER_DIR)/hello_nyx.o

$(USER_DIR)/nyxrt.o: $(USER_DIR)/nyxrt.c $(USER_DIR)/nyxrt.h
	$(CC) $(NYXCFLAGS) -c $< -o $@

$(USER_DIR)/hello_nyx.o: $(USER_DIR)/hello_nyx.c $(USER_DIR)/nyxrt.h
	$(CC) $(NYXCFLAGS) -c $< -o $@

$(USER_DIR)/libc.o: $(USER_DIR)/libc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/init.o: $(USER_DIR)/init.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Generate initramfs C header
MKINITRAMFS = ../tools/mkinitramfs.py
initramfs_data.h: $(MKINITRAMFS) $(USER_ELFS)
	python3 $(MKINITRAMFS) $@ c

initramfs.o: initramfs.c initramfs_data.h

# SMP trampoline — flat binary embedded as ELF object
trampoline.bin: trampoline.asm
	$(AS) -f bin -o $@ $<

trampoline.o: trampoline.bin
	$(LD) -r -b binary -m elf_x86_64 -o $@ $<

clean:
	rm -f *.o *.bin initramfs_data.h trampoline.bin $(USER_DIR)/*.o $(USER_DIR)/*.elf $(USER_DIR)/hello.o

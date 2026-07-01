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
        memory.o heap.o slab.o process.o syscall.o vfs.o ext2.o ata.o net.o udp.o \
        ip.o arp.o ethernet.o rtl8139.o icmp.o timer.o keyboard.o screen.o \
        serial.o umoddi3.o paging.o switch.o stubs.o dhcp.o \
        vbe.o fb.o mouse.o gui.o font.o compositor.o speaker.o tcp.o \
        sb16.o terminal_win.o fileman_win.o paint_win.o taskman_win.o \
        editor_win.o imageview_win.o soundtest_win.o calc_win.o \
        elf.o initramfs.o rtc.o apic.o dns.o http.o

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
USER_ELFS = $(USER_DIR)/hello.elf $(USER_DIR)/init.elf

$(USER_DIR)/hello.elf: $(USER_DIR)/hello.asm
	nasm -f elf64 $< -o $(USER_DIR)/hello.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/hello.o

$(USER_DIR)/init.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o
	$(LD) -nostdlib -m elf_x86_64 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o

$(USER_DIR)/crt0.o: $(USER_DIR)/crt0.asm
	nasm -f elf64 $< -o $@

$(USER_DIR)/libc.o: $(USER_DIR)/libc.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/init.o: $(USER_DIR)/init.c $(USER_DIR)/libc.h $(USER_DIR)/syscall.h
	$(CC) -std=gnu99 -Os -ffreestanding -nostdlib -m64 -mno-red-zone -I$(USER_DIR) -c $< -o $@

# Generate initramfs C header
MKINITRAMFS = ../tools/mkinitramfs.py
initramfs_data.h: $(MKINITRAMFS) $(USER_ELFS)
	python3 $(MKINITRAMFS) $@ c

initramfs.o: initramfs.c initramfs_data.h

clean:
	rm -f *.o *.bin initramfs_data.h $(USER_DIR)/*.o $(USER_DIR)/*.elf $(USER_DIR)/hello.o

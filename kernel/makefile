# ============================================================
# kernel/Makefile - Compilación del kernel NyxOS v2.1.1
# ============================================================

TARGET = i686-elf
CC = $(TARGET)-gcc
AS = nasm
LD = $(TARGET)-ld

CFLAGS = -std=gnu99 -ffreestanding -Os -Wall -Wextra \
         -nostdlib -nostartfiles -nodefaultlibs \
         -fno-stack-protector -fno-pie -m32 \
         -ffunction-sections -fdata-sections \
         -I.

DOOM_CFLAGS = $(CFLAGS) -I doom_src -D DOOMGENERIC -D CMAP256=1

ASFLAGS = -f elf32

LDFLAGS = -T linker.ld -nostdlib -m elf_i386

OBJS_KERNEL = boot.o kernel.o gdt.o gdt_flush.o idt.o idt_load.o isr.o isr_stubs.o irq.o \
       memory.o heap.o process.o syscall.o vfs.o ext2.o ata.o net.o udp.o \
       ip.o arp.o ethernet.o rtl8139.o icmp.o timer.o keyboard.o screen.o \
       serial.o umoddi3.o paging.o switch.o dhcp.o \
       vga_graphics.o doom_nyxos.o \
       vbe.o fb.o mouse.o gui.o font.o compositor.o speaker.o tcp.o \
       sb16.o doom_nyxos_sound.o terminal_win.o fileman_win.o \
        elf.o initramfs.o rtc.o

DOOM_OBJS = \
    doom_src/am_map.o \
    doom_src/d_event.o \
    doom_src/d_items.o \
    doom_src/d_iwad.o \
    doom_src/d_loop.o \
    doom_src/d_main.o \
    doom_src/d_mode.o \
    doom_src/d_net.o \
    doom_src/doom_utils.o \
    doom_src/doomdef.o \
    doom_src/doomstat.o \
    doom_src/dstrings.o \
    doom_src/dummy.o \
    doom_src/f_finale.o \
    doom_src/f_wipe.o \
    doom_src/g_game.o \
    doom_src/hu_lib.o \
    doom_src/hu_stuff.o \
    doom_src/i_cdmus.o \
    doom_src/i_endoom.o \
    doom_src/i_input.o \
    doom_src/i_joystick.o \
    doom_src/i_scale.o \
    doom_src/i_sound.o \
    doom_src/i_system.o \
    doom_src/i_timer.o \
    doom_src/i_video.o \
    doom_src/info.o \
    doom_src/m_argv.o \
    doom_src/m_bbox.o \
    doom_src/m_cheat.o \
    doom_src/m_config.o \
    doom_src/m_controls.o \
    doom_src/m_fixed.o \
    doom_src/m_menu.o \
    doom_src/m_misc.o \
    doom_src/m_random.o \
    doom_src/memio.o \
    doom_src/p_ceilng.o \
    doom_src/p_doors.o \
    doom_src/p_enemy.o \
    doom_src/p_floor.o \
    doom_src/p_inter.o \
    doom_src/p_lights.o \
    doom_src/p_map.o \
    doom_src/p_maputl.o \
    doom_src/p_mobj.o \
    doom_src/p_plats.o \
    doom_src/p_pspr.o \
    doom_src/p_saveg.o \
    doom_src/p_setup.o \
    doom_src/p_sight.o \
    doom_src/p_spec.o \
    doom_src/p_switch.o \
    doom_src/p_telept.o \
    doom_src/p_tick.o \
    doom_src/p_user.o \
    doom_src/r_bsp.o \
    doom_src/r_data.o \
    doom_src/r_draw.o \
    doom_src/r_main.o \
    doom_src/r_plane.o \
    doom_src/r_segs.o \
    doom_src/r_sky.o \
    doom_src/r_things.o \
    doom_src/s_sound.o \
    doom_src/sha1.o \
    doom_src/sounds.o \
    doom_src/st_lib.o \
    doom_src/st_stuff.o \
    doom_src/statdump.o \
    doom_src/tables.o \
    doom_src/v_video.o \
    doom_src/w_checksum.o \
    doom_src/w_file.o \
    doom_src/w_file_stdc.o \
    doom_src/w_main.o \
    doom_src/w_wad.o \
    doom_src/wi_stuff.o \
    doom_src/z_zone.o

OBJS = $(OBJS_KERNEL) $(DOOM_OBJS) doom1_wad.o

all: $(USER_ELFS) nyx-kernel.bin

nyx-kernel.bin: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

# Kernel C files use normal CFLAGS
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# DOOM source files use DOOM_CFLAGS (with -I doom_src)
doom_src/%.o: doom_src/%.c
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

# Assembly files
%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

# Special rule for doom_nyxos.c (in kernel root, not doom_src)
doom_nyxos.o: doom_nyxos.c
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

# Special rule for doom_nyxos_sound.c (SB16 DOOM sound module)
doom_nyxos_sound.o: doom_nyxos_sound.c
	$(CC) $(DOOM_CFLAGS) -c $< -o $@

# Generate WAD object file
doom1_wad.o: doom1.wad
	$(TARGET)-objcopy -I binary -O elf32-i386 -B i386 \
		--rename-section .data=.rodata,alloc,load,readonly,data,contents $< $@

# User-space programs
USER_DIR = ../user
USER_ELFS = $(USER_DIR)/hello.elf $(USER_DIR)/init.elf

$(USER_DIR)/hello.elf: $(USER_DIR)/hello.asm
	$(AS) $(ASFLAGS) $< -o $(USER_DIR)/hello.o
	$(LD) -nostdlib -m elf_i386 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/hello.o

$(USER_DIR)/init.elf: $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o
	$(LD) -nostdlib -m elf_i386 -e _start -Ttext 0x10000 -o $@ $(USER_DIR)/crt0.o $(USER_DIR)/libc.o $(USER_DIR)/init.o

$(USER_DIR)/crt0.o: $(USER_DIR)/crt0.asm
	$(AS) $(ASFLAGS) $< -o $@

$(USER_DIR)/libc.o: $(USER_DIR)/libc.c $(USER_DIR)/libc.h
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -I$(USER_DIR) -c $< -o $@

$(USER_DIR)/init.o: $(USER_DIR)/init.c
	$(CC) $(CFLAGS) -ffreestanding -nostdlib -I$(USER_DIR) -c $< -o $@

# Generate initramfs C header (depends on user ELF files)
MKINITRAMFS = ../tools/mkinitramfs.py
initramfs_data.h: $(MKINITRAMFS) $(USER_ELFS)
	python3 $(MKINITRAMFS) $@ c

initramfs.o: initramfs.c initramfs_data.h

clean:
	rm -f *.o *.bin doom_src/*.o initramfs_data.h $(USER_DIR)/*.o $(USER_DIR)/*.elf $(USER_DIR)/hello.o

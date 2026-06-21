#!/bin/bash
export PATH="/mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/cross/bin:$PATH"
cd /mnt/c/Users/kzh/Desktop/Proyectos/nyx-os/kernel
i686-elf-gcc -std=gnu99 -ffreestanding -Os -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs -fno-stack-protector -fno-pie -m32 -ffunction-sections -fdata-sections -I. -I doom_src -c vga_graphics.c -o vga_graphics.o 2>&1
echo "---"
i686-elf-gcc -std=gnu99 -ffreestanding -Os -Wall -Wextra -nostdlib -nostartfiles -nodefaultlibs -fno-stack-protector -fno-pie -m32 -ffunction-sections -fdata-sections -I. -I doom_src -c doom_nyxos.c -o doom_nyxos.o 2>&1

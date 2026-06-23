@echo off
echo NyxOS - DOOM Edition
echo ====================
"C:\Program Files\qemu\qemu-system-i386.exe" -cdrom "%~dp0NyxOS.iso" -m 256M -no-reboot -display sdl

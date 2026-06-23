@echo off
title NyxOS - QEMU VNC Server
echo ========================================
echo  NyxOS - Arrancando con VNC + Serial
echo ========================================
echo.
echo 1. Esta ventana es para ESCRIBIR comandos
echo 2. Abre VNC Viewer y conecta a: localhost:0
echo 3. Escribe 'doom' en esta ventana y pulsa Enter
echo 4. Veras DOOM en la ventana de VNC Viewer
echo.
echo NOTA: Ctrl+Alt+G libera el teclado/raton en VNC
echo ========================================
echo.
"C:\Program Files\qemu\qemu-system-i386w.exe" -cdrom "%~dp0NyxOS.iso" -m 256M -no-reboot -vga std -vnc :0 -serial stdio
pause

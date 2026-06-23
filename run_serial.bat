@echo off
echo NyxOS - Serial Terminal Mode
echo ============================
echo Type 'doom' at the shell prompt
echo Use Ctrl+Alt+G to release cursor from QEMU
echo.
"C:\Program Files\qemu\qemu-system-i386.exe" -cdrom "%~dp0NyxOS.iso" -m 256M -no-reboot -nographic
pause

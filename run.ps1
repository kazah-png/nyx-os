param(
    [ValidateSet('gui','serial','net','debug')]
    [string]$Mode = 'gui',
    [switch]$Sound,
    # CPU count. Every harness before v5.8.90 omitted -smp, so no application
    # processor had ever actually run; 4 is what `cpus` is meant to show off.
    [int]$Cpus = 4
)

$root = $PSScriptRoot
$kernel = "$root\kernel\nyx-kernel.bin"
$iso = "$root\NyxOS.iso"

if (!(Test-Path $kernel)) {
    Write-Host "[ERROR] nyx-kernel.bin not found. Run build.ps1 first." -ForegroundColor Red
    exit 1
}

# Build ISO if it doesn't exist (needed for 64-bit kernel boot)
if (!(Test-Path $iso)) {
    Write-Host "[INFO] Creating bootable ISO (needed for x86_64 kernel)..." -ForegroundColor Yellow
    wsl bash -c "cd $(wslpath -a "$root") && mkdir -p iso/boot/grub && cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    boot
}
EOF
cp kernel/nyx-kernel.bin iso/boot/ && grub-mkrescue -o NyxOS.iso iso/ > /dev/null 2>&1 && echo 'ISO created'" 2>&1 | Select-String -Pattern "error|Error|FAIL|ISO created" -SimpleMatch
}

if (!(Test-Path $iso)) {
    Write-Host "[ERROR] Failed to create bootable ISO." -ForegroundColor Red
    exit 1
}

# Try common QEMU install paths
$qemuPaths = @(
    "C:\Program Files\qemu",
    "C:\Program Files\qemu\i386",
    "$env:LOCALAPPDATA\Programs\qemu",
    "$env:USERPROFILE\scoop\apps\qemu\current"
)
foreach ($p in $qemuPaths) {
    if (Test-Path "$p\qemu-system-x86_64.exe") {
        $env:Path = "$p;$env:Path"
        break
    }
}

if (!(Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] qemu-system-x86_64 not found." -ForegroundColor Red
    Write-Host "Install QEMU (https://www.qemu.org/download/#windows) or add it to PATH" -ForegroundColor Yellow
    exit 1
}

$argsList = @(
    "-cdrom", $iso,
    "-m", "256M",
    "-smp", "$Cpus",
    "-no-reboot",
    "-cpu", "qemu64"
)

switch ($Mode) {
    'gui'    { $argsList += "-display", "sdl"; $argsList += "-serial", "file:qemu_serial.txt" }
    'serial' { $argsList += "-nographic"; $argsList += "-serial", "stdio" }
    'net'    { $argsList += "-display", "sdl"; $argsList += "-serial", "file:qemu_serial.txt"; $argsList += "-nic", "user,model=rtl8139" }
    'debug'  { $argsList += "-display", "sdl"; $argsList += "-serial", "file:qemu_serial.txt"; $argsList += "-d", "cpu_reset,int" }
}

if ($Sound -or $Mode -eq 'net') {
    $argsList += "-audiodev", "dsound,id=audio0"
    $argsList += "-device", "sb16,audiodev=audio0"
}

Write-Host "=== Launching NyxOS (mode: $Mode) ===" -ForegroundColor Cyan
Write-Host "QEMU: $(qemu-system-x86_64 --version | Select-Object -First 1)" -ForegroundColor Gray

& qemu-system-x86_64 @argsList

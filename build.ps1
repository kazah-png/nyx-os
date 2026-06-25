param([switch]$Clean)

$root = $PSScriptRoot
# Convert Windows path to WSL path dynamically
$drive = $root[0].ToString().ToLower()
$wslRoot = "/mnt/$drive" + $root.Substring(2) -replace '\\', '/'
$cross = "$wslRoot/cross/bin"

function wsl-build {
    param([string]$Target)
    $cmd = "PATH=$cross" + ':/usr/bin:/bin make -C kernel ' + $Target
    $out = wsl bash -c "$cmd" 2>&1
    $global:lastExit = $LASTEXITCODE
    return $out
}

Write-Host "=== Building NyxOS Kernel ===" -ForegroundColor Cyan

if ($Clean) {
    Write-Host "[*] Cleaning..." -ForegroundColor Yellow
    wsl-build -Target 'clean' | Out-Null
}

Write-Host "[*] Compiling kernel..." -ForegroundColor Yellow
$result = wsl-build

if ($global:lastExit -ne 0) {
    Write-Host "[FAIL] Build failed (exit $($global:lastExit))" -ForegroundColor Red
    $result
    exit 1
}

$kernelBin = "$root\kernel\nyx-kernel.bin"
if (Test-Path $kernelBin) {
    $size = (Get-Item $kernelBin).Length
    Write-Host "[OK] nyx-kernel.bin ($([math]::Round($size/1024, 1)) KB)" -ForegroundColor Green
} else {
    Write-Host "[FAIL] nyx-kernel.bin not found!" -ForegroundColor Red
    exit 1
}

# Build ISO for 64-bit kernel boot
Write-Host "[*] Creating bootable ISO..." -ForegroundColor Yellow
wsl bash -c "cd $wslRoot && mkdir -p iso/boot/grub && cat > iso/boot/grub/grub.cfg << 'EOF'
set timeout=5
set default=0
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    boot
}
EOF
cp kernel/nyx-kernel.bin iso/boot/ && grub-mkrescue -o NyxOS.iso iso/ > /dev/null 2>&1" 2>&1

$isoFile = "$root\NyxOS.iso"
if (Test-Path $isoFile) {
    $size = (Get-Item $isoFile).Length
    Write-Host "[OK] NyxOS.iso ($([math]::Round($size/1024, 1)) KB)" -ForegroundColor Green
} else {
    Write-Host "[WARN] ISO creation failed (grub-mkrescue required)" -ForegroundColor Yellow
}

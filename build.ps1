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

# Surface compiler warnings even on a SUCCESSFUL build. Previously $result was printed
# only on failure, so a warning backlog stayed hidden behind a green build (this is the
# gap that let ~39 warnings accumulate unseen; see the LTS 0-warn gate). Now every build
# reports its warning count, and lists them so they never pile up invisibly again.
$warns = @($result | Select-String -Pattern 'warning:|error:')
if ($warns.Count -gt 0) {
    Write-Host "[WARN] $($warns.Count) compiler warning(s):" -ForegroundColor Yellow
    $warns | ForEach-Object { Write-Host "  $($_.Line)" -ForegroundColor DarkYellow }
} elseif ($global:lastExit -eq 0) {
    Write-Host "[OK] 0 warnings" -ForegroundColor Green
}

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
insmod all_video
menuentry 'NyxOS' {
    multiboot2 /boot/nyx-kernel.bin
    module2 /boot/nyx-kernel.bin nyxkernel.bin
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

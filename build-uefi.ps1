param([switch]$Clean)

# build-uefi.ps1 — compile the kernel, then emit a UEFI-bootable NyxOS ISO.
# The regular build.ps1 makes a legacy-BIOS ISO; this one targets modern UEFI machines
# (GRUB x86_64-efi, GOP framebuffer). Flash NyxOS-uefi.iso to a USB with Rufus (DD mode).
# See docs/REAL_HARDWARE.md for firmware settings and the physical-boot checklist.

$root = $PSScriptRoot

Write-Host "=== Building NyxOS (UEFI) ===" -ForegroundColor Cyan

# 1) Compile the kernel + BIOS ISO via the normal build (also confirms a 0-warn build).
if ($Clean) { & "$root\build.ps1" -Clean } else { & "$root\build.ps1" }
if ($LASTEXITCODE -ne 0) { Write-Host "[FAIL] kernel build failed" -ForegroundColor Red; exit 1 }

# 2) Wrap the freshly built kernel into a GRUB-EFI ISO (grub-mkrescue runs in WSL).
$drive   = $root[0].ToString().ToLower()
$wslRoot = "/mnt/$drive" + $root.Substring(2) -replace '\\', '/'

Write-Host "[*] Creating UEFI ISO..." -ForegroundColor Yellow
wsl bash "$wslRoot/tools/mkuefi.sh" "$wslRoot/NyxOS-uefi.iso"

$iso = "$root\NyxOS-uefi.iso"
if (Test-Path $iso) {
    $size = [math]::Round((Get-Item $iso).Length / 1KB, 1)
    Write-Host "[OK] NyxOS-uefi.iso ($size KB)" -ForegroundColor Green
    Write-Host "     Flash to USB with Rufus (DD image mode). Secure Boot must be OFF." -ForegroundColor Green
} else {
    Write-Host "[FAIL] UEFI ISO not produced (need grub-efi-amd64-bin + mtools in WSL)" -ForegroundColor Red
    exit 1
}

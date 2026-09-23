# setup_and_flash.ps1 — Poudre Trail CYD full setup (Windows / PowerShell)
#
# Run from the repo root in an Administrator PowerShell:
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#   .\scripts\setup_and_flash.ps1
#
# What it does:
#   1. Detects the CYD COM port (CP2102 / CH340 / CH9102)
#   2. Detects the SD card drive letter
#   3. Full-formats SD card as FAT32 (label: POUDRE)
#   4. Creates correct SD directory layout and copies all game content
#   5. Builds and flashes firmware via PlatformIO
#   6. Opens serial monitor at 115200 baud

#Requires -RunAsAdministrator

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Push-Location $RepoRoot

function Write-Info  { param($m) Write-Host "[INFO]  $m" -ForegroundColor Green }
function Write-Warn  { param($m) Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Err   { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

# ─── 1. Detect CYD COM port ─────────────────────────────────────────────────

Write-Info "Scanning for CYD COM port..."

# Common ESP32 USB-serial chips: Silicon Labs CP210x, WCH CH340/CH9102, FTDI
$espChips = @('CP210','CH340','CH9102','USB-SERIAL','USB Serial','FTDI')

$comPort = $null
$candidates = Get-PnpDevice -Class 'Ports' -Status OK -ErrorAction SilentlyContinue |
              Where-Object { $name = $_.FriendlyName; $espChips | Where-Object { $name -match $_ } }

if ($candidates) {
    $device = $candidates | Select-Object -First 1
    # Extract COM number from FriendlyName e.g. "Silicon Labs CP210x (COM4)"
    if ($device.FriendlyName -match '\(COM(\d+)\)') {
        $comPort = "COM$($Matches[1])"
        Write-Info "Found CYD at: $comPort  ($($device.FriendlyName))"
    }
}

if (-not $comPort) {
    Write-Warn "Could not auto-detect an ESP32 COM port."
    Write-Host ""
    Write-Host "Available COM ports:"
    Get-PnpDevice -Class 'Ports' -Status OK -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "  $($_.FriendlyName)" }
    Write-Host ""
    $comPort = Read-Host "Enter COM port (e.g. COM4)"
    if (-not ($comPort -match '^COM\d+$')) { Write-Err "Invalid COM port: $comPort" }
}

# ─── 2. Detect SD card drive ─────────────────────────────────────────────────

Write-Info "Scanning for SD card / removable drive..."

$sdDrive = $null
$removable = Get-Disk | Where-Object { $_.BusType -in @('USB','SD','MMC') -and $_.IsSystem -eq $false }

if ($removable) {
    $disk = $removable | Select-Object -First 1
    Write-Info "Found removable disk: Disk $($disk.Number) — $([math]::Round($disk.Size/1GB,1)) GB  $($disk.FriendlyName)"

    # Get drive letter of any partition on that disk
    $part = $null
    try {
        $part = Get-Partition -DiskNumber $disk.Number -ErrorAction SilentlyContinue |
                Where-Object { $_.DriveLetter -and $_.DriveLetter -ne [char]0 } |
                Select-Object -First 1
    } catch { $part = $null }

    if ($part) {
        $sdDrive = "$($part.DriveLetter):"
        Write-Info "SD card drive letter: $sdDrive"
    } else {
        Write-Warn "Disk found but no drive letter assigned yet. Will format the whole disk."
    }

    # Store disk number for Format-Volume
    $sdDiskNumber = $disk.Number
} else {
    Write-Warn "No removable disk found automatically."
    Write-Host ""
    Write-Host "Connected disks:"
    Get-Disk | Format-Table Number, FriendlyName, BusType, Size, PartitionStyle
    Write-Host ""
    $sdDiskNumber = [int](Read-Host "Enter Disk Number for SD card (e.g. 1)")
    $disk = Get-Disk -Number $sdDiskNumber
}

# Safety: refuse internal / system disks
if ($disk.IsSystem -or $disk.IsBoot) {
    Write-Err "SAFETY STOP: Disk $sdDiskNumber is a system/boot disk. Refusing to format."
}
if ($disk.Size -gt 64GB) {
    Write-Warn "Disk $sdDiskNumber is $([math]::Round($disk.Size/1GB,1)) GB — larger than expected for an SD card."
}

# ─── 3. Confirm ─────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Yellow
Write-Host "  About to FULLY WIPE Disk ${sdDiskNumber}: $($disk.FriendlyName)" -ForegroundColor Yellow
Write-Host "  Size: $([math]::Round($disk.Size/1GB,2)) GB" -ForegroundColor Yellow
Write-Host "  ALL DATA ON THIS DISK WILL BE DESTROYED." -ForegroundColor Yellow
Write-Host "  CYD serial port: $comPort" -ForegroundColor Yellow
Write-Host "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" -ForegroundColor Yellow
Write-Host ""
$confirm = Read-Host "Type YES to continue"
if ($confirm -ne 'YES') { Write-Info "Aborted."; exit 0 }

# ─── 4. Wipe and format FAT32 ────────────────────────────────────────────────

Write-Info "Clearing disk $sdDiskNumber..."
Clear-Disk -Number $sdDiskNumber -RemoveData -RemoveOEM -Confirm:$false

Write-Info "Initialising MBR partition table..."
Initialize-Disk -Number $sdDiskNumber -PartitionStyle MBR -Confirm:$false

Write-Info "Creating single FAT32 partition..."
$newPart = New-Partition -DiskNumber $sdDiskNumber -UseMaximumSize -MbrType FAT32 -AssignDriveLetter
$sdDrive = "$($newPart.DriveLetter):"

Write-Info "Formatting $sdDrive as FAT32 (label: POUDRE)..."
Format-Volume -DriveLetter $newPart.DriveLetter -FileSystem FAT32 -NewFileSystemLabel 'POUDRE' -Confirm:$false | Out-Null

Write-Info "SD card formatted. Drive letter: $sdDrive"

# ─── 5. Copy game content ────────────────────────────────────────────────────

Write-Info "Creating SD directory structure..."
New-Item -ItemType Directory -Path "$sdDrive\art"         -Force | Out-Null
New-Item -ItemType Directory -Path "$sdDrive\data\events" -Force | Out-Null
New-Item -ItemType Directory -Path "$sdDrive\saves"       -Force | Out-Null

Write-Info "Copying BMP art files (12 files)..."
Copy-Item "$RepoRoot\data\art\*.bmp" "$sdDrive\art\" -Force

Write-Info "Copying CSV game content..."
Copy-Item "$RepoRoot\data\locations.csv" "$sdDrive\data\" -Force
Copy-Item "$RepoRoot\data\events.csv"    "$sdDrive\data\" -Force
Copy-Item "$RepoRoot\data\trades.csv"    "$sdDrive\data\" -Force

Write-Info "Copying event body text files..."
Copy-Item "$RepoRoot\data\events\*.txt" "$sdDrive\data\events\" -Force

Write-Info "SD card contents:"
Get-ChildItem -Path $sdDrive -Recurse | Where-Object { -not $_.PSIsContainer } |
    Select-Object -ExpandProperty FullName |
    ForEach-Object { Write-Host "  $($_ -replace [regex]::Escape($sdDrive), '')" }

Write-Info "Flushing write cache..."
# Dismount and remount to force a flush
$vol = Get-Volume -DriveLetter $newPart.DriveLetter
Write-Info "All files written to $sdDrive"

Write-Host ""
Write-Info "You can now safely eject the SD card and insert it into the CYD."
Read-Host "Press Enter when the SD card is in the CYD and the CYD is connected via USB"

# ─── 6. Build ────────────────────────────────────────────────────────────────

Write-Info "Building firmware..."
& pio run
if ($LASTEXITCODE -ne 0) { Write-Err "PlatformIO build failed." }

# ─── 7. Flash ────────────────────────────────────────────────────────────────

Write-Info "Flashing to CYD at $comPort..."
Write-Host ""
Write-Host "  If upload hangs at 'Connecting...', hold the BOOT button on the"
Write-Host "  CYD for 2 seconds then release it to enter bootloader mode."
Write-Host ""

$env:PLATFORMIO_UPLOAD_PORT = $comPort
& pio run -t upload --upload-port $comPort
if ($LASTEXITCODE -ne 0) { Write-Err "Flash failed. Check $comPort and try again." }

# ─── 8. Serial monitor ───────────────────────────────────────────────────────

Write-Host ""
Write-Info "Flash complete! Opening serial monitor (Ctrl+C to exit)..."
Write-Host ""
Write-Host "  Expected first lines on the CYD:"
Write-Host "    SD content loaded"
Write-Host "    (or: SD init failed - fallback world)"
Write-Host ""
Start-Sleep -Seconds 1

& pio device monitor --port $comPort --baud 115200

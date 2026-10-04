# setup_and_flash.ps1 - Poudre Trail CYD setup script (Windows / PowerShell)
#
# Run from the repo root in an Administrator PowerShell:
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
#   .\scripts\setup_and_flash.ps1
#
# Steps (select at the menu):
#   1  Full setup     - format SD + copy data + build + flash
#   2  Format SD only - wipe and partition the SD card
#   3  Copy data only - copy game files to an already-formatted POUDRE drive
#   4  Build + flash  - compile and flash firmware (SD card not needed)
#   5  SD prep only   - format SD + copy data (no flash)

#Requires -RunAsAdministrator

$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Write-Info  { param($m) Write-Host "[INFO]  $m" -ForegroundColor Green }
function Write-Warn  { param($m) Write-Host "[WARN]  $m" -ForegroundColor Yellow }
function Write-Err   { param($m) Write-Host "[ERROR] $m" -ForegroundColor Red; exit 1 }

# Wrapper: use pio if on PATH, otherwise fall back to python -m platformio
function Invoke-Pio {
    param([string[]]$PioArgs)
    if (Get-Command pio -ErrorAction SilentlyContinue) {
        & pio @PioArgs
    } else {
        & python -m platformio @PioArgs
    }
    return $LASTEXITCODE
}

Write-Info "Working directory: $RepoRoot"
Write-Host ""

# --- Step selection menu -----------------------------------------------------

Write-Host "  What would you like to do?" -ForegroundColor Cyan
Write-Host "  [1]  Full setup     - format SD + copy data + build + flash" -ForegroundColor Cyan
Write-Host "  [2]  Format SD only - wipe and re-partition the SD card" -ForegroundColor Cyan
Write-Host "  [3]  Copy data only - copy game files to POUDRE drive" -ForegroundColor Cyan
Write-Host "  [4]  Build + flash  - compile and flash firmware only" -ForegroundColor Cyan
Write-Host "  [5]  SD prep only   - format SD + copy data (no flash)" -ForegroundColor Cyan
Write-Host ""

$choice = Read-Host "Enter choice (1-5)"
switch ($choice) {
    '1' { $doFormat = $true;  $doCopy = $true;  $doFlash = $true  }
    '2' { $doFormat = $true;  $doCopy = $false; $doFlash = $false }
    '3' { $doFormat = $false; $doCopy = $true;  $doFlash = $false }
    '4' { $doFormat = $false; $doCopy = $false; $doFlash = $true  }
    '5' { $doFormat = $true;  $doCopy = $true;  $doFlash = $false }
    default { Write-Err "Invalid choice: $choice" }
}

Write-Host ""

# --- Detect CYD COM port (only needed for flash) -----------------------------

$comPort = $null

if ($doFlash) {
    Write-Info "Scanning for CYD COM port..."

    $espChips = @('CP210','CH340','CH9102','USB-SERIAL','USB Serial','FTDI')
    $candidates = Get-PnpDevice -Class 'Ports' -Status OK -ErrorAction SilentlyContinue |
                  Where-Object { $name = $_.FriendlyName; $espChips | Where-Object { $name -match $_ } }

    if ($candidates) {
        $device = $candidates | Select-Object -First 1
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
}

# --- Detect SD card (only needed for format or copy) -------------------------

$sdDrive      = $null
$disk         = $null
$sdDiskNumber = $null

if ($doFormat -or $doCopy) {

    if ($doFormat) {

        # Need the raw disk for formatting
        Write-Info "Scanning for SD card / removable drive..."

        $allRemovable = @(Get-Disk | Where-Object { $_.BusType -in @('USB','SD','MMC') -and $_.IsSystem -eq $false })
        $sdKeywords   = @('SD','MMC','Card','Reader','Transcend','SanDisk','Kingston','Samsung')
        $sdCandidates = @($allRemovable | Where-Object { $n = $_.FriendlyName; $sdKeywords | Where-Object { $n -match $_ } })

        if ($sdCandidates.Count -eq 1) {
            $disk = $sdCandidates[0]
        } elseif ($allRemovable.Count -eq 1) {
            $disk = $allRemovable[0]
        } else {
            Write-Warn "Multiple removable disks found. Please choose the SD card:"
            Write-Host ""
            $allRemovable | ForEach-Object {
                $gb = [math]::Round($_.Size / 1GB, 1)
                Write-Host "  Disk $($_.Number) - $gb GB - $($_.FriendlyName) [$($_.BusType)]"
            }
            Write-Host ""
            $sdDiskNumber = [int](Read-Host "Enter Disk Number for SD card")
            $disk = Get-Disk -Number $sdDiskNumber
        }

        if (-not $disk) {
            Write-Warn "No removable disk found automatically."
            Write-Host ""
            Get-Disk | Format-Table Number, FriendlyName, BusType, Size, PartitionStyle
            Write-Host ""
            $sdDiskNumber = [int](Read-Host "Enter Disk Number for SD card (e.g. 1)")
            $disk = Get-Disk -Number $sdDiskNumber
        }

        $sdDiskNumber = $disk.Number
        $diskGB = [math]::Round($disk.Size / 1GB, 1)
        Write-Info "Selected SD disk: Disk $sdDiskNumber - $diskGB GB  $($disk.FriendlyName)"

        # Safety: refuse system/boot disks
        if ($disk.IsSystem -or $disk.IsBoot) {
            Write-Err "SAFETY STOP: Disk $sdDiskNumber is a system/boot disk. Refusing to format."
        }
        if ($disk.Size -gt 64GB) {
            Write-Warn "Disk $sdDiskNumber is $diskGB GB - larger than expected for an SD card."
        }

    } else {

        # Copy-only: find the POUDRE-labelled FAT32 volume
        Write-Info "Looking for POUDRE volume (FAT32, labelled POUDRE)..."
        $poudreVol = Get-Volume -ErrorAction SilentlyContinue |
                     Where-Object { $_.FileSystemLabel -eq 'POUDRE' -and $_.DriveLetter } |
                     Select-Object -First 1
        if ($poudreVol) {
            $sdDrive = "$($poudreVol.DriveLetter):"
            Write-Info "Found POUDRE volume at $sdDrive"
        } else {
            Write-Warn "Could not find a POUDRE-labelled FAT32 volume."
            Get-Volume | Format-Table DriveLetter, FileSystemLabel, FileSystem, Size
            $letter = Read-Host "Enter the drive letter of the POUDRE SD partition (e.g. E)"
            $sdDrive = "${letter}:"
        }
    }
}

# --- Confirm before format ---------------------------------------------------

if ($doFormat) {
    Write-Host ""
    Write-Host "----------------------------------------------------------------" -ForegroundColor Yellow
    Write-Host "  About to FULLY WIPE Disk ${sdDiskNumber}: $($disk.FriendlyName)" -ForegroundColor Yellow
    Write-Host "  Size: $([math]::Round($disk.Size/1GB,2)) GB" -ForegroundColor Yellow
    Write-Host "  ALL DATA ON THIS DISK WILL BE DESTROYED." -ForegroundColor Yellow
    if ($comPort) { Write-Host "  CYD serial port: $comPort" -ForegroundColor Yellow }
    Write-Host "----------------------------------------------------------------" -ForegroundColor Yellow
    Write-Host ""
    $confirm = Read-Host "Type YES to continue"
    if ($confirm -ne 'YES') { Write-Info "Aborted."; exit 0 }
}

# --- Format SD ---------------------------------------------------------------

if ($doFormat) {
    Write-Info "Wiping Disk $sdDiskNumber and creating two partitions via diskpart..."
    Write-Info "  Partition 1: 16 GB FAT32  (POUDRE)  - game / CYD projects"
    Write-Info "  Partition 2: remainder exFAT (STORAGE) - general use"

    $diskpartScript = @"
select disk $sdDiskNumber
clean
create partition primary size=16384
select partition 1
format fs=fat32 label=POUDRE quick
assign
create partition primary
select partition 2
format fs=exfat label=STORAGE quick
assign
exit
"@

    $diskpartScript | diskpart | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE -ne 0) { Write-Err "diskpart failed (exit code $LASTEXITCODE)." }

    Write-Info "Waiting for Windows to assign drive letters..."
    Start-Sleep -Seconds 3

    $sdDrive = $null
    try {
        $fmtPart = Get-Partition -DiskNumber $sdDiskNumber -ErrorAction SilentlyContinue |
                   Where-Object { $_.DriveLetter -and $_.DriveLetter -ne [char]0 } |
                   Select-Object -First 1
        if ($fmtPart) { $sdDrive = "$($fmtPart.DriveLetter):" }
    } catch {}

    if (-not $sdDrive) {
        Write-Warn "Could not detect drive letter automatically."
        Get-Volume | Format-Table DriveLetter, FileSystemLabel, FileSystem, Size
        $letter = Read-Host "Enter the drive letter assigned to the POUDRE partition (e.g. E)"
        $sdDrive = "${letter}:"
    }

    Write-Info "SD card formatted. POUDRE drive: $sdDrive"
}

# --- Copy game content -------------------------------------------------------

if ($doCopy) {
    Write-Info "Copying game content to $sdDrive ..."

    New-Item -ItemType Directory -Path "$sdDrive\art"         -Force | Out-Null
    New-Item -ItemType Directory -Path "$sdDrive\data\events" -Force | Out-Null
    New-Item -ItemType Directory -Path "$sdDrive\saves"       -Force | Out-Null

    Write-Info "Copying BMP art files..."
    Copy-Item "$RepoRoot\data\art\*.bmp" "$sdDrive\art\" -Force

    Write-Info "Copying CSV game content..."
    Copy-Item "$RepoRoot\data\locations.csv" "$sdDrive\data\" -Force
    Copy-Item "$RepoRoot\data\events.csv"    "$sdDrive\data\" -Force
    Copy-Item "$RepoRoot\data\trades.csv"    "$sdDrive\data\" -Force

    Write-Info "Copying event body text files..."
    Copy-Item "$RepoRoot\data\events\*.txt" "$sdDrive\data\events\" -Force

    Write-Info "Contents of $sdDrive :"
    Get-ChildItem -Path $sdDrive -Recurse | Where-Object { -not $_.PSIsContainer } |
        Select-Object -ExpandProperty FullName |
        ForEach-Object { Write-Host "  $($_ -replace [regex]::Escape($sdDrive), '')" }

    Write-Info "All files written to $sdDrive"
}

# --- Prompt to move SD card to CYD (only if SD step ran and flash follows) ---

if (($doFormat -or $doCopy) -and $doFlash) {
    Write-Host ""
    Write-Info "You can now safely eject the SD card and insert it into the CYD."
    Read-Host "Press Enter when the SD card is in the CYD and the CYD is connected via USB"
}

# --- Build + flash -----------------------------------------------------------

if ($doFlash) {
    Write-Info "Building firmware..."
    $rc = Invoke-Pio @('run')
    if ($rc -ne 0) { Write-Err "PlatformIO build failed." }

    Write-Info "Flashing to CYD at $comPort..."
    Write-Host ""
    Write-Host "  If upload hangs at 'Connecting...', hold the BOOT button on the"
    Write-Host "  CYD for 2 seconds then release it to enter bootloader mode."
    Write-Host ""

    $env:PLATFORMIO_UPLOAD_PORT = $comPort
    $rc = Invoke-Pio @('run', '-t', 'upload', '--upload-port', $comPort)
    if ($rc -ne 0) { Write-Err "Flash failed. Check $comPort and try again." }

    Write-Host ""
    Write-Info "Flash complete! Opening serial monitor (Ctrl+C to exit)..."
    Write-Host ""
    Write-Host "  Expected first lines on the CYD:"
    Write-Host "    SD content loaded"
    Write-Host "    (or: SD init failed - fallback world)"
    Write-Host ""
    Start-Sleep -Seconds 1

    Invoke-Pio @('device', 'monitor', '--port', $comPort, '--baud', '115200') | Out-Null
}

Write-Host ""
Write-Info "Done."

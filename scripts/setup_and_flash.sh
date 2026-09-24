#!/usr/bin/env bash
# setup_and_flash.sh - Poudre Trail CYD setup script (Linux / macOS)
#
# Run from the repo root:
#   chmod +x scripts/setup_and_flash.sh
#   sudo ./scripts/setup_and_flash.sh
#
# Steps (select at the menu):
#   1  Full setup     - format SD + copy data + build + flash
#   2  Format SD only - wipe and partition the SD card
#   3  Copy data only - copy game files to an already-formatted POUDRE drive
#   4  Build + flash  - compile and flash firmware (SD card not needed)
#   5  SD prep only   - format SD + copy data (no flash)
#
# Requirements:
#   PlatformIO Core:  pip install platformio
#   dosfstools:       sudo apt install dosfstools          (Linux)
#                     brew install dosfstools               (macOS - uses newfs_msdos)
#   exfatprogs:       sudo apt install exfatprogs          (Linux - for STORAGE partition)
#                     macOS: newfs_exfat is built-in
#   sudo access for formatting the SD card

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

info "Working directory: $REPO_ROOT"
echo ""

# --- Step selection menu -----------------------------------------------------

echo -e "${YELLOW}  What would you like to do?${NC}"
echo "  [1]  Full setup     - format SD + copy data + build + flash"
echo "  [2]  Format SD only - wipe and re-partition the SD card"
echo "  [3]  Copy data only - copy game files to POUDRE drive"
echo "  [4]  Build + flash  - compile and flash firmware only"
echo "  [5]  SD prep only   - format SD + copy data (no flash)"
echo ""
read -rp "Enter choice (1-5): " CHOICE

case "$CHOICE" in
    1) DO_FORMAT=1; DO_COPY=1; DO_FLASH=1 ;;
    2) DO_FORMAT=1; DO_COPY=0; DO_FLASH=0 ;;
    3) DO_FORMAT=0; DO_COPY=1; DO_FLASH=0 ;;
    4) DO_FORMAT=0; DO_COPY=0; DO_FLASH=1 ;;
    5) DO_FORMAT=1; DO_COPY=1; DO_FLASH=0 ;;
    *) error "Invalid choice: $CHOICE" ;;
esac

echo ""

# --- Detect CYD serial port (only needed for flash) --------------------------

SERIAL_PORT=""

if [ "$DO_FLASH" -eq 1 ]; then
    info "Scanning for CYD serial port..."
    for candidate in /dev/ttyUSB0 /dev/ttyUSB1 /dev/ttyACM0 /dev/ttyACM1 /dev/ttyUSB2 /dev/ttyACM2; do
        if [ -e "$candidate" ]; then
            SERIAL_PORT="$candidate"
            info "Found serial port: $SERIAL_PORT"
            break
        fi
    done

    if [ -z "$SERIAL_PORT" ]; then
        warn "No /dev/ttyUSB* or /dev/ttyACM* found automatically."
        echo ""
        echo "Available ports:"
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  (none visible)"
        echo ""
        read -rp "Enter serial port manually (e.g. /dev/ttyUSB0): " SERIAL_PORT
        [ -e "$SERIAL_PORT" ] || error "Port $SERIAL_PORT does not exist."
    fi
fi

# --- Detect SD card block device (only needed for format or copy) ------------

SD_DEV=""
POUDRE_PART=""

if [ "$DO_FORMAT" -eq 1 ] || [ "$DO_COPY" -eq 1 ]; then

    if [ "$DO_FORMAT" -eq 1 ]; then

        info "Scanning for SD card / removable block device..."
        for dev in /dev/sda /dev/sdb /dev/sdc /dev/sdd /dev/mmcblk0 /dev/mmcblk1; do
            if [ -b "$dev" ]; then
                SYS_NAME=$(basename "$dev")
                REMOVABLE_PATH="/sys/block/${SYS_NAME}/removable"
                if [ -f "$REMOVABLE_PATH" ] && [ "$(cat "$REMOVABLE_PATH")" = "1" ]; then
                    SD_DEV="$dev"
                    info "Found removable device: $SD_DEV"
                    break
                fi
            fi
        done

        if [ -z "$SD_DEV" ]; then
            warn "No removable block device found automatically."
            echo ""
            lsblk -o NAME,SIZE,RM,TYPE,LABEL,MOUNTPOINT 2>/dev/null || ls /dev/sd* /dev/mmcblk* 2>/dev/null || echo "  (none visible)"
            echo ""
            read -rp "Enter SD card device (e.g. /dev/sdb or /dev/mmcblk0): " SD_DEV
            [ -b "$SD_DEV" ] || error "Device $SD_DEV is not a block device."
        fi

        # Safety: refuse system mount points
        MOUNTS=$(lsblk -no MOUNTPOINT "$SD_DEV" 2>/dev/null || true)
        for mp in $MOUNTS; do
            case "$mp" in
                /|/boot|/home|/usr|/var|/etc)
                    error "SAFETY STOP: $SD_DEV appears to be mounted at '$mp'. Refusing to format."
                    ;;
            esac
        done

        DEV_GB=$(lsblk -bno SIZE "$SD_DEV" 2>/dev/null | head -1 | awk '{printf "%.1f", $1/1024/1024/1024}')
        info "Selected: $SD_DEV  (${DEV_GB} GB)"
        if (( $(echo "$DEV_GB > 512" | bc -l 2>/dev/null || echo 0) )); then
            warn "$SD_DEV is ${DEV_GB} GB - larger than expected for an SD card."
        fi

    else
        # Copy-only: find mounted POUDRE volume
        info "Looking for mounted POUDRE volume..."
        if command -v lsblk &>/dev/null; then
            POUDRE_MOUNT=$(lsblk -o LABEL,MOUNTPOINT 2>/dev/null | awk '$1=="POUDRE" && $2!="" {print $2; exit}')
        else
            POUDRE_MOUNT=$(mount | grep -i poudre | awk '{print $3}' | head -1)
        fi

        if [ -n "$POUDRE_MOUNT" ]; then
            info "Found POUDRE volume at $POUDRE_MOUNT"
        else
            warn "Could not find a mounted POUDRE volume."
            lsblk -o NAME,LABEL,FSTYPE,SIZE,MOUNTPOINT 2>/dev/null || df -h
            read -rp "Enter mountpoint of POUDRE partition (e.g. /media/user/POUDRE): " POUDRE_MOUNT
        fi
    fi
fi

# --- Confirm before format ---------------------------------------------------

if [ "$DO_FORMAT" -eq 1 ]; then
    echo ""
    echo -e "${YELLOW}----------------------------------------------------------------${NC}"
    echo -e "${YELLOW}  About to FULLY WIPE: $SD_DEV${NC}"
    echo -e "${YELLOW}  Partition 1: 16 GB FAT32  (POUDRE)  - game / CYD projects${NC}"
    echo -e "${YELLOW}  Partition 2: remainder exFAT (STORAGE) - general use${NC}"
    echo -e "${YELLOW}  ALL DATA ON THIS DISK WILL BE DESTROYED.${NC}"
    if [ -n "$SERIAL_PORT" ]; then
        echo -e "${YELLOW}  CYD serial port: $SERIAL_PORT${NC}"
    fi
    echo -e "${YELLOW}----------------------------------------------------------------${NC}"
    echo ""
    read -rp "Type YES to continue: " CONFIRM
    [ "$CONFIRM" = "YES" ] || { info "Aborted."; exit 0; }
fi

# --- Format SD ---------------------------------------------------------------

if [ "$DO_FORMAT" -eq 1 ]; then
    info "Unmounting any existing partitions on $SD_DEV..."
    for part in "${SD_DEV}"[0-9] "${SD_DEV}p"[0-9]; do
        [ -b "$part" ] || continue
        if mount | grep -q "^$part "; then
            sudo umount "$part" && info "  Unmounted $part" || warn "  Could not unmount $part"
        fi
    done
    if mount | grep -q "^$SD_DEV "; then
        sudo umount "$SD_DEV" || true
    fi

    info "Wiping partition table on $SD_DEV..."
    sudo dd if=/dev/zero of="$SD_DEV" bs=1M count=2 status=none

    info "Creating two partitions: 16 GB FAT32 (POUDRE) + remainder exFAT (STORAGE)..."
    # Partition in MB: 16384 MB = 16 GB for POUDRE; rest for STORAGE
    sudo parted -s "$SD_DEV" \
        mklabel msdos \
        mkpart primary fat32 1MiB 16385MiB \
        mkpart primary 16385MiB 100%

    sleep 1
    sudo partprobe "$SD_DEV" 2>/dev/null || sudo blockdev --rereadpt "$SD_DEV" 2>/dev/null || true
    sleep 1

    # Determine partition names (sdb -> sdb1/sdb2, mmcblk0 -> mmcblk0p1/mmcblk0p2)
    if [[ "$SD_DEV" == *mmcblk* ]]; then
        POUDRE_PART="${SD_DEV}p1"
        STORAGE_PART="${SD_DEV}p2"
    else
        POUDRE_PART="${SD_DEV}1"
        STORAGE_PART="${SD_DEV}2"
    fi

    for i in {1..5}; do
        [ -b "$POUDRE_PART" ] && break
        sleep 1
    done
    [ -b "$POUDRE_PART" ] || error "Partition $POUDRE_PART did not appear."

    info "Formatting $POUDRE_PART as FAT32 (label: POUDRE)..."
    if command -v mkfs.fat &>/dev/null; then
        sudo mkfs.fat -F 32 -n POUDRE "$POUDRE_PART"
    elif command -v newfs_msdos &>/dev/null; then
        sudo newfs_msdos -F 32 -v POUDRE "$POUDRE_PART"
    else
        error "No FAT32 formatter found. Install dosfstools: sudo apt install dosfstools"
    fi

    info "Formatting $STORAGE_PART as exFAT (label: STORAGE)..."
    if command -v mkfs.exfat &>/dev/null; then
        sudo mkfs.exfat -n STORAGE "$STORAGE_PART"
    elif command -v newfs_exfat &>/dev/null; then
        sudo newfs_exfat -v STORAGE "$STORAGE_PART"
    else
        warn "No exFAT formatter found. STORAGE partition left unformatted."
        warn "Install: sudo apt install exfatprogs"
    fi

    # Mount POUDRE for the copy step
    POUDRE_MOUNT="$(mktemp -d /tmp/poudre_sd_XXXX)"
    sudo mount "$POUDRE_PART" "$POUDRE_MOUNT"
    info "POUDRE partition mounted at $POUDRE_MOUNT"
fi

# --- Copy game content -------------------------------------------------------

if [ "$DO_COPY" -eq 1 ]; then

    # If we just formatted, POUDRE_MOUNT is already set. For copy-only it was set above.
    MOUNT="$POUDRE_MOUNT"

    # Set up cleanup trap to unmount when done
    _mounted_by_us=0
    if [ "$DO_FORMAT" -eq 1 ]; then
        _mounted_by_us=1
    fi

    cleanup_mount() {
        if [ "$_mounted_by_us" -eq 1 ] && mountpoint -q "$MOUNT" 2>/dev/null; then
            sudo umount "$MOUNT" && info "SD card unmounted cleanly."
            rm -rf "$MOUNT"
        fi
    }
    trap cleanup_mount EXIT

    info "Copying game content to $MOUNT ..."

    sudo mkdir -p "$MOUNT/art"
    sudo mkdir -p "$MOUNT/data/events"
    sudo mkdir -p "$MOUNT/saves"

    info "Copying BMP art files..."
    sudo cp "$REPO_ROOT"/data/art/*.bmp "$MOUNT/art/"

    info "Copying CSV game content..."
    sudo cp "$REPO_ROOT/data/locations.csv" "$MOUNT/data/"
    sudo cp "$REPO_ROOT/data/events.csv"    "$MOUNT/data/"
    sudo cp "$REPO_ROOT/data/trades.csv"    "$MOUNT/data/"

    info "Copying event body text files..."
    sudo cp "$REPO_ROOT"/data/events/*.txt "$MOUNT/data/events/"

    info "Contents of $MOUNT:"
    find "$MOUNT" -mindepth 1 | sort | sed "s|$MOUNT||" | while read -r f; do
        echo "  $f"
    done

    info "Syncing to SD card..."
    sudo sync

    info "All files written."
fi

# --- Prompt to move SD card to CYD (only if SD step ran and flash follows) ---

if { [ "$DO_FORMAT" -eq 1 ] || [ "$DO_COPY" -eq 1 ]; } && [ "$DO_FLASH" -eq 1 ]; then
    echo ""
    info "You can now safely eject the SD card and insert it into the CYD."
    read -rp "Press Enter when the SD card is in the CYD and the CYD is connected via USB..."
fi

# --- Build + flash -----------------------------------------------------------

if [ "$DO_FLASH" -eq 1 ]; then
    info "Building firmware..."
    pio run

    info "Flashing to CYD at $SERIAL_PORT..."
    echo ""
    echo "  If upload hangs at 'Connecting...', hold the BOOT button on the"
    echo "  CYD for 2 seconds then release it to enter bootloader mode."
    echo ""

    PLATFORMIO_UPLOAD_PORT="$SERIAL_PORT" pio run -t upload --upload-port "$SERIAL_PORT"

    echo ""
    info "Flash complete! Opening serial monitor (Ctrl+C to exit)..."
    echo ""
    echo "  Expected first lines on the CYD:"
    echo "    SD content loaded"
    echo "    (or: SD init failed - fallback world)"
    echo ""
    sleep 1
    pio device monitor --port "$SERIAL_PORT" --baud 115200
fi

echo ""
info "Done."

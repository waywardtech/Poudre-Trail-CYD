#!/usr/bin/env bash
# setup_and_flash.sh — Poudre Trail CYD full setup script
#
# Run from the repo root on the machine the CYD and SD card reader are plugged into:
#   chmod +x scripts/setup_and_flash.sh
#   ./scripts/setup_and_flash.sh
#
# What it does:
#   1. Detects the CYD serial port and the SD card block device
#   2. Formats the SD card FAT32 (full wipe)
#   3. Creates the correct SD directory layout and copies all game content
#   4. Builds and flashes the firmware via PlatformIO
#   5. Opens the serial monitor at 115200 baud
#
# Requirements (must be installed on your machine):
#   - PlatformIO Core: pip install platformio
#   - mkdosfs / dosfstools: sudo apt install dosfstools  (Linux)
#                            brew install dosfstools       (macOS: newfs_msdos used instead)
#   - rsync (usually pre-installed)
#   - sudo access for formatting the SD card

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ─── 1. Detect CYD serial port ───────────────────────────────────────────────

info "Scanning for CYD serial port..."

SERIAL_PORT=""

# ESP32 CP2102 / CH340 / CH9102 USB-serial adapters appear as ttyUSB* or ttyACM*
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
    ls /dev/tty{USB,ACM}* 2>/dev/null || echo "  (none visible)"
    echo ""
    read -rp "Enter serial port manually (e.g. /dev/ttyUSB0): " SERIAL_PORT
    [ -e "$SERIAL_PORT" ] || error "Port $SERIAL_PORT does not exist."
fi

# ─── 2. Detect SD card block device ─────────────────────────────────────────

info "Scanning for SD card block device..."

SD_DEV=""

# Look for removable USB storage or mmcblk devices
# Check /sys for removable flag to distinguish SD readers from internal disks
for dev in /dev/sda /dev/sdb /dev/sdc /dev/sdd /dev/mmcblk0 /dev/mmcblk1; do
    if [ -b "$dev" ]; then
        # Check if it's marked removable
        SYS_NAME=$(basename "$dev")
        REMOVABLE_PATH="/sys/block/${SYS_NAME}/removable"
        if [ -f "$REMOVABLE_PATH" ] && [ "$(cat "$REMOVABLE_PATH")" = "1" ]; then
            SD_DEV="$dev"
            info "Found removable SD device: $SD_DEV"
            break
        fi
    fi
done

# Fallback: list block devices for manual selection
if [ -z "$SD_DEV" ]; then
    warn "No removable block device found automatically."
    echo ""
    echo "Available block devices:"
    lsblk -o NAME,SIZE,RM,TYPE,LABEL,MOUNTPOINT 2>/dev/null || ls /dev/sd* /dev/mmcblk* 2>/dev/null || echo "  (none visible)"
    echo ""
    read -rp "Enter SD card device (e.g. /dev/sda or /dev/mmcblk0): " SD_DEV
    [ -b "$SD_DEV" ] || error "Device $SD_DEV is not a block device."
fi

# Safety check — refuse to format anything mounted at / or /boot
MOUNTS=$(lsblk -no MOUNTPOINT "$SD_DEV" 2>/dev/null || mount | grep "^$SD_DEV" | awk '{print $3}')
for mp in $MOUNTS; do
    case "$mp" in
        /|/boot|/home|/usr|/var|/etc)
            error "SAFETY STOP: $SD_DEV appears to be mounted at '$mp'. Refusing to format."
            ;;
    esac
done

echo ""
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo -e "${YELLOW}  About to FULLY WIPE and FORMAT: $SD_DEV${NC}"
echo -e "${YELLOW}  All existing data on this device will be destroyed.${NC}"
echo -e "${YELLOW}  Serial port: $SERIAL_PORT${NC}"
echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
echo ""
read -rp "Type YES to continue: " CONFIRM
[ "$CONFIRM" = "YES" ] || { info "Aborted."; exit 0; }

# ─── 3. Unmount any existing partitions ──────────────────────────────────────

info "Unmounting $SD_DEV partitions..."
for part in "${SD_DEV}"* "${SD_DEV}p"*; do
    [ "$part" = "$SD_DEV" ] && continue
    [ -b "$part" ] || continue
    if mount | grep -q "^$part "; then
        sudo umount "$part" && info "  Unmounted $part" || warn "  Could not unmount $part"
    fi
done
# Also unmount the device itself if mounted
if mount | grep -q "^$SD_DEV "; then
    sudo umount "$SD_DEV" && info "  Unmounted $SD_DEV" || warn "  Could not unmount $SD_DEV"
fi

# ─── 4. Wipe and create FAT32 partition ──────────────────────────────────────

info "Wiping partition table on $SD_DEV..."
# Zero the first 1 MB to clear any existing partition table / MBR
sudo dd if=/dev/zero of="$SD_DEV" bs=1M count=1 status=none

info "Writing new MBR partition table with single FAT32 partition..."
# Use a here-doc to feed fdisk: create one primary FAT32 partition using the whole disk
sudo fdisk "$SD_DEV" <<'FDISK_CMDS' 2>/dev/null || true
o
n
p
1


t
b
w
FDISK_CMDS

# Let the kernel re-read the partition table
sleep 1
sudo partprobe "$SD_DEV" 2>/dev/null || sudo blockdev --rereadpt "$SD_DEV" 2>/dev/null || true
sleep 1

# Determine partition device name (sda→sda1, mmcblk0→mmcblk0p1)
if [[ "$SD_DEV" == *mmcblk* ]]; then
    PART="${SD_DEV}p1"
else
    PART="${SD_DEV}1"
fi

# Wait for the partition node to appear
for i in {1..5}; do
    [ -b "$PART" ] && break
    sleep 1
done
[ -b "$PART" ] || error "Partition $PART did not appear after partitioning."

info "Formatting $PART as FAT32 (label: POUDRE)..."
if command -v mkfs.fat &>/dev/null; then
    sudo mkfs.fat -F 32 -n POUDRE "$PART"
elif command -v newfs_msdos &>/dev/null; then
    # macOS
    sudo newfs_msdos -F 32 -v POUDRE "$PART"
else
    error "No FAT32 formatter found. Install dosfstools: sudo apt install dosfstools"
fi

# ─── 5. Mount and copy game content ─────────────────────────────────────────

MOUNTPOINT="$(mktemp -d /tmp/poudre_sd_XXXX)"
info "Mounting $PART at $MOUNTPOINT..."
sudo mount "$PART" "$MOUNTPOINT"

# Ensure we unmount on exit even if something fails
cleanup() {
    if mountpoint -q "$MOUNTPOINT" 2>/dev/null; then
        sudo umount "$MOUNTPOINT" && info "SD card unmounted cleanly."
    fi
    rm -rf "$MOUNTPOINT"
}
trap cleanup EXIT

info "Creating SD directory structure..."
sudo mkdir -p "$MOUNTPOINT/art"
sudo mkdir -p "$MOUNTPOINT/data/events"
sudo mkdir -p "$MOUNTPOINT/saves"

info "Copying BMP art files..."
sudo cp "$REPO_ROOT"/data/art/*.bmp "$MOUNTPOINT/art/"

info "Copying CSV game content..."
sudo cp "$REPO_ROOT/data/locations.csv" "$MOUNTPOINT/data/"
sudo cp "$REPO_ROOT/data/events.csv"    "$MOUNTPOINT/data/"
sudo cp "$REPO_ROOT/data/trades.csv"    "$MOUNTPOINT/data/"

info "Copying event body text files..."
sudo cp "$REPO_ROOT"/data/events/*.txt  "$MOUNTPOINT/data/events/"

info "SD card contents:"
find "$MOUNTPOINT" -not -path "$MOUNTPOINT" | sort | sed "s|$MOUNTPOINT/||" | while read -r f; do
    echo "  /$f"
done

info "Syncing to SD card..."
sudo sync

# cleanup trap will unmount here

# ─── 6. Build firmware ───────────────────────────────────────────────────────

# Temporarily remove SD card so it's safe to remove
info "You can now safely remove the SD card and insert it into the CYD."
echo ""

info "Building firmware..."
cd "$REPO_ROOT"
pio run

# ─── 7. Flash ────────────────────────────────────────────────────────────────

info "Flashing to CYD at $SERIAL_PORT..."
echo ""
echo "  If the upload hangs at 'Connecting...', hold the BOOT button on the"
echo "  CYD for 2 seconds then release it to enter bootloader mode."
echo ""

PLATFORMIO_UPLOAD_PORT="$SERIAL_PORT" pio run -t upload --upload-port "$SERIAL_PORT"

# ─── 8. Serial monitor ───────────────────────────────────────────────────────

echo ""
info "Flash complete. Opening serial monitor (Ctrl+C or Ctrl+] to exit)..."
echo ""
echo "  Expected first lines:"
echo "    SD content loaded"
echo "    (or: SD init failed - fallback world)"
echo ""
sleep 1

pio device monitor --port "$SERIAL_PORT" --baud 115200

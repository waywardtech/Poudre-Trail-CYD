#!/usr/bin/env bash
# Flash Poudre Trail to a CYD that is plugged into another machine (a Libre La Frite,
# a Pi, ...) over SSH, then reset the board and print its boot log. Modelled on
# CYD-Diagnostic's tools/deploy.sh.
#
#   tools/deploy_remote.sh user@host                 # install tools, flash, show boot log
#   tools/deploy_remote.sh user@host --env esp32-2432S028R-st7789
#   tools/deploy_remote.sh user@host --port /dev/ttyUSB0 --key ~/.ssh/id_ed25519
#   tools/deploy_remote.sh user@host --monitor 60    # seconds of serial to capture
#
# Everything lives under ~/poudre-trail on the remote host.
set -euo pipefail

TARGET="${1:-}"
[ -n "$TARGET" ] || { echo "usage: $0 user@host [--env ENV] [--port DEV] [--key FILE] [--monitor SECS]" >&2; exit 2; }
shift
ENVIRONMENT="esp32-2432S028R"; PORT=""; KEY="${SSH_KEY:-}"; SECS=25
while [ $# -gt 0 ]; do
  case "$1" in
    --env) ENVIRONMENT="${2:-}"; shift ;;
    --port) PORT="${2:-}"; shift ;;
    --key) KEY="${2:-}"; shift ;;
    --monitor) SECS="${2:-}"; shift ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
  shift
done

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SSH_OPTS="-o ConnectTimeout=10 -o StrictHostKeyChecking=accept-new -o BatchMode=yes"
[ -n "$KEY" ] && SSH_OPTS="$SSH_OPTS -i $KEY -o IdentitiesOnly=yes"
# shellcheck disable=SC2086
run() { ssh $SSH_OPTS "$TARGET" "$@"; }
say() { printf '\n\033[1m== %s\033[0m\n' "$1"; }

say "Copying the project to $TARGET:~/poudre-trail"
run 'mkdir -p ~/poudre-trail'
tar -C "$ROOT" --exclude=.git --exclude=.pio --exclude='test/test_engine' -czf - . \
  | ssh $SSH_OPTS "$TARGET" 'tar -C ~/poudre-trail -xzf -'

say "Finding the CYD and PlatformIO"
run "PORT='$PORT' ENVIRONMENT='$ENVIRONMENT' SECS='$SECS' bash -s" <<'REMOTE'
set -e
export PATH="$HOME/.local/bin:$PATH"
cd ~/poudre-trail
if ! command -v pio >/dev/null 2>&1; then
  echo "installing PlatformIO (pip --user)"; python3 -m pip install --user -q platformio
fi
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1 || true)"
fi
[ -n "$PORT" ] || { echo "no /dev/ttyUSB* or /dev/ttyACM* found - is the CYD plugged in?"; exit 1; }
id -nG | tr ' ' '\n' | grep -qx dialout || echo "WARNING: $USER is not in the dialout group (sudo usermod -aG dialout $USER, then log in again)"
echo "CYD on $PORT, env $ENVIRONMENT"

echo; echo "== Building and flashing"
pio run -e "$ENVIRONMENT" -t upload --upload-port "$PORT"

echo; echo "== Boot log (${SECS}s) - look for [TFT] and [SD] lines"
python3 - "$PORT" "$SECS" <<'PY'
import sys, time
try:
    import serial
except ImportError:
    import subprocess; subprocess.check_call([sys.executable, "-m", "pip", "install", "--user", "-q", "pyserial"]); import serial
port, secs = sys.argv[1], float(sys.argv[2])
s = serial.Serial(port, 115200, timeout=0.2)
s.dtr = False; s.rts = True; time.sleep(0.15); s.rts = False   # pulse EN: reset the board
end = time.time() + secs
while time.time() < end:
    line = s.readline()
    if line:
        print(line.decode("utf-8", "replace").rstrip())
PY
REMOTE

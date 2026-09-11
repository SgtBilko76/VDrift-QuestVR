#!/bin/bash
# Run the VDrift dedicated server.
#
#   ./run-server.sh                                  # defaults below
#   TRACKS=ruudskogen,estoril88 LAPS=3 BOTS=4 ./run-server.sh
#   ./run-server.sh --port 28601                     # extra vdrift-server options
#
# The server listens on UDP 28600 (open it inbound), waits in the lobby until
# MINPLAYERS have joined plus LOBBYWAIT seconds, races, shows the results, and
# goes round again on the next track in TRACKS. ONCE=1 stops after one race.
set -euo pipefail

PREFIX="${PREFIX:-$HOME/vdserver}"
TRACKS="${TRACKS:-ruudskogen,estoril88,paulricard88}"
LAPS="${LAPS:-3}"
BOTS="${BOTS:-3}"
MINPLAYERS="${MINPLAYERS:-1}"
LOBBYWAIT="${LOBBYWAIT:-20}"
FINISHWAIT="${FINISHWAIT:-60}"
MAXRACETIME="${MAXRACETIME:-0}"
PORT="${PORT:-28600}"

if command -v ss >/dev/null && ss -lun 2>/dev/null | grep -q ":$PORT "; then
    echo "something is already listening on UDP $PORT; stop it first:" >&2
    echo "    pkill -x vdrift-server" >&2
    exit 1
fi

BIN="$PREFIX/bin/vdrift-server"
[ -x "$BIN" ] || { echo "no server binary at $BIN - run setup-linux-server.sh" >&2; exit 1; }
[ -f "$PREFIX/env" ] && . "$PREFIX/env"

ARGS=(--port "$PORT" --track "$TRACKS" --laps "$LAPS" --bots "$BOTS"
      --min-players "$MINPLAYERS" --lobby-wait "$LOBBYWAIT" --finish-wait "$FINISHWAIT"
      --max-race-time "$MAXRACETIME")
[ "${ONCE:-0}" = "1" ] && ARGS+=(--once)

exec "$BIN" "${ARGS[@]}" "$@"

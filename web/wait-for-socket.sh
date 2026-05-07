#!/bin/sh
# wait-for-socket.sh - waits for CLI unix socket before starting Node API
SOCK="${CLI_SOCKET:-/tmp/vepc.sock}"
TIMEOUT="${WAIT_FOR_SOCKET_TIMEOUT:-30}"

# Optionally wait for DB DNS/name/port to be reachable before waiting for socket
DB_HOST="${DB_HOST:-db}"
DB_PORT="${DB_PORT:-3306}"
DB_WAIT="${WAIT_FOR_DB:-1}"

echo "wait-for-socket: waiting for socket ${SOCK} (timeout ${TIMEOUT}s)"
if [ "$DB_WAIT" != "0" ]; then
  echo "wait-for-socket: waiting for DB ${DB_HOST}:${DB_PORT} to be reachable"
  node ./wait-for-db.js --host "$DB_HOST" --port "$DB_PORT" --timeout 60 || echo "wait-for-socket: DB wait failed or timed out"
fi
elapsed=0
while [ ! -S "${SOCK}" ] && [ "$elapsed" -lt "$TIMEOUT" ]; do
  echo "wait-for-socket: not present yet... ${elapsed}s"
  sleep 1
  elapsed=$((elapsed+1))
done

if [ -S "${SOCK}" ]; then
  echo "wait-for-socket: socket present, starting server"
  exec node api/server.js
else
  echo "wait-for-socket: timeout after ${TIMEOUT}s waiting for ${SOCK}" >&2
  exit 1
fi

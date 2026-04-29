#!/bin/sh
# wait-for-socket.sh - waits for CLI unix socket before starting Node API
SOCK="${CLI_SOCKET:-/tmp/vepc.sock}"
TIMEOUT="${WAIT_FOR_SOCKET_TIMEOUT:-30}"

echo "wait-for-socket: waiting for socket ${SOCK} (timeout ${TIMEOUT}s)"
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

#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LOG_FILE="$SCRIPT_DIR/build/logs/vepc.log"

cd "$SCRIPT_DIR" || exit 1

clear
echo -e "\e[1;35m===== LIVE LOG =====\e[0m"
tail -f "$LOG_FILE"

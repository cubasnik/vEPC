#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VEPCC="$BUILD_DIR/vepc"
LOG_FILE="$BUILD_DIR/logs/vepc.log"

# Обязательно запускаем из папки проекта — vepc ищет config/ и build/logs/ отсюда
cd "$SCRIPT_DIR" || exit 1

clear
echo -e "\e[1;35m===== vEPC SERVER =====\e[0m"
echo ""

while true; do
    "$VEPCC"
    echo -e "\e[1;31m[MAIN]\e[0m Сервер упал! Перезапуск через 2 сек..."
    sleep 2
done

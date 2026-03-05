#!/bin/bash
# monitoring.sh — 4 именованные вкладки в Windows Terminal
# Порядок: [📊 Dashboard] [⌨ vEPC CLI] [🖥 vEPC SERVER] [📄 LIVE LOG]

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VEPC_CLI="$BUILD_DIR/vepc-cli"
SOCKET_PATH="/tmp/vepc.sock"
LOG_DIR="$BUILD_DIR/logs"
LOG_FILE="$LOG_DIR/vepc.log"
MAX_LOG_SIZE=$((10*1024*1024))

# -------------------
# Проверка setup
# -------------------
if [ ! -d "$SCRIPT_DIR/config" ]; then
    echo -e "\e[1;31m[ERROR]\e[0m Папка config/ не найдена."
    echo "Запусти сначала: ./setup.sh"
    exit 1
fi

# -------------------
# Создание директорий
# -------------------
mkdir -p "$LOG_DIR"
rm -f "$SOCKET_PATH"

# -------------------
# Проверка бинарников
# -------------------
for BIN in "$BUILD_DIR/vepc" "$BUILD_DIR/vepc-cli"; do
    if [ ! -f "$BIN" ]; then
        echo -e "\e[1;31m[ERROR]\e[0m Бинарник $BIN не найден. Сначала соберите проект."
        exit 1
    fi
done

# -------------------
# Ротация логов в фоне
# -------------------
rotate_logs_bg() {
    while true; do
        if [ -f "$LOG_FILE" ] && [ "$(stat -c%s "$LOG_FILE")" -ge "$MAX_LOG_SIZE" ]; then
            mv "$LOG_FILE" "$LOG_FILE.$(date +%Y%m%d%H%M%S)"
            touch "$LOG_FILE"
        fi
        sleep 5
    done
}
rotate_logs_bg &
ROTATE_PID=$!

# -------------------
# Поиск wt.exe
# -------------------
find_wt() {
    command -v wt.exe 2>/dev/null && return
    local WIN_USER
    WIN_USER=$(cmd.exe /c "echo %USERNAME%" 2>/dev/null | tr -d '\r')
    for base in \
        "/mnt/c/Users/$WIN_USER/AppData/Local/Microsoft/WindowsApps" \
        "/mnt/c/Program Files/WindowsApps" \
        "/mnt/c/Windows/System32"
    do
        local wt_path
        wt_path=$(find "$base" -maxdepth 3 -name "wt.exe" 2>/dev/null | head -1)
        [ -n "$wt_path" ] && echo "$wt_path" && return
    done
}

WT=$(find_wt)
if [ -z "$WT" ]; then
    echo -e "\e[1;31m[ERROR]\e[0m wt.exe не найден."
    echo "Найди вручную: find /mnt/c -name 'wt.exe' 2>/dev/null"
    kill "$ROTATE_PID" 2>/dev/null
    exit 1
fi

chmod +x "$SCRIPT_DIR"/panel_*.sh

# -------------------
# Запуск 4 вкладок
# Порядок: Dashboard → CLI → SERVER → LOG
# --title — имя вкладки вместо "Default"
# focus-tab --target 0 — фокус на Dashboard
# -------------------
"$WT" \
    new-tab --title "📊 Dashboard" \
        wsl.exe -e bash -c "cd '$SCRIPT_DIR' && exec ./panel_dashboard.sh" \
    \; new-tab --title "⌨  vEPC CLI" \
        wsl.exe -e bash -c "cd '$SCRIPT_DIR' && exec ./panel_cli.sh" \
    \; new-tab --title "🖥  vEPC SERVER" \
        wsl.exe -e bash -c "cd '$SCRIPT_DIR' && exec ./panel_server.sh" \
    \; new-tab --title "📄 LIVE LOG" \
        wsl.exe -e bash -c "cd '$SCRIPT_DIR' && exec ./panel_log.sh" \
    \; focus-tab --target 0

echo ""
echo -e "\e[1;32m✓ Открыты 4 вкладки Windows Terminal:\e[0m"
echo "  [1] 📊 Dashboard"
echo "  [2] ⌨  vEPC CLI"
echo "  [3] 🖥  vEPC SERVER"
echo "  [4] 📄 LIVE LOG"
echo ""
echo "Переключение: Ctrl+Tab / Ctrl+1..4"
echo "Остановка: закрой окно или выполни stop в CLI"

trap "kill $ROTATE_PID 2>/dev/null" EXIT

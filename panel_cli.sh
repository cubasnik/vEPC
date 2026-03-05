#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
VEPC_CLI="$BUILD_DIR/vepc-cli"
SOCKET_PATH="/tmp/vepc.sock"
CONFIG_INTERFACES="$SCRIPT_DIR/config/interfaces.conf"

cd "$SCRIPT_DIR" || exit 1

clear
echo -e "\e[1;35m===== vEPC CLI =====\e[0m"
echo ""

# --- Команды CLI ---
echo -e "\e[1;33m Команды CLI:\e[0m"
echo -e "  \e[1;36mstatus\e[0m   — состояние MME, SGSN, PLMN"
echo -e "  \e[1;36mlogs\e[0m     — последние 20 записей лога"
echo -e "  \e[1;36mstate\e[0m    — кол-во PDP и UE контекстов"
echo -e "  \e[1;36mshow\e[0m     — текущая конфигурация"
echo -e "  \e[1;36mstop\e[0m     — остановить vEPC"
echo -e "  \e[1;36mexit\e[0m     — выйти из CLI"
echo ""

# --- Интерфейсы из конфига ---
echo -e "\e[1;33m Интерфейсы (config/interfaces.conf):\e[0m"
if [ -f "$CONFIG_INTERFACES" ]; then
    section=""
    proto=""; ip=""; port=""; to=""
    flush_section() {
        [ -z "$section" ] && return
        printf "  \e[1;36m%-8s\e[0m  proto=\e[0;37m%-18s\e[0m  %s:%s  → \e[0;90m%s\e[0m\n" \
               "$section" "$proto" "$ip" "$port" "$to"
    }
    while IFS= read -r line; do
        if [[ "$line" =~ ^\[(.+)\]$ ]]; then
            flush_section
            section="${BASH_REMATCH[1]}"; proto=""; ip=""; port=""; to=""
        elif [[ "$line" =~ ^[[:space:]]*protocol[[:space:]]*=[[:space:]]*(.+)$ ]]; then
            proto="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^[[:space:]]*ip[[:space:]]*=[[:space:]]*(.+)$ ]]; then
            ip="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^[[:space:]]*port[[:space:]]*=[[:space:]]*(.+)$ ]]; then
            port="${BASH_REMATCH[1]}"
        elif [[ "$line" =~ ^[[:space:]]*to[[:space:]]*=[[:space:]]*(.+)$ ]]; then
            to="${BASH_REMATCH[1]}"
        fi
    done < "$CONFIG_INTERFACES"
    flush_section
else
    echo -e "  \e[1;31m[WARN]\e[0m config/interfaces.conf не найден"
fi

echo ""
echo -e "\e[0;90m────────────────────────────────────\e[0m"
echo ""

while true; do
    if [ ! -e "$SOCKET_PATH" ]; then
        sleep 0.5
        continue
    fi
    "$VEPC_CLI"
    echo -e "\e[1;33m[CLI]\e[0m CLI отключился. Переподключение через 1 сек..."
    sleep 1
done

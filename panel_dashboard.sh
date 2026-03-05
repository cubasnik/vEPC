#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
LOG_FILE="$BUILD_DIR/logs/vepc.log"
SOCKET_PATH="/tmp/vepc.sock"
MAX_LOG_SIZE=$((10*1024*1024))

cd "$SCRIPT_DIR" || exit 1

progress_bar() {
    local value=$1 max=$2 width=$3
    [[ "$max" -eq 0 ]] && max=1
    local filled=$(( value * width / max ))
    local empty=$(( width - filled ))
    local bar=""
    for ((i=0; i<filled; i++)); do bar+="█"; done
    for ((i=0; i<empty;  i++)); do bar+="░"; done
    printf "%s" "$bar"
}

draw_static() {
    clear
    echo -e "\e[1;35m===== vEPC Dashboard + CLI Traffic + Network Pulse =====\e[0m"
    echo -e "\e[1;32m[SERVER]\e[0m"
    echo -e "\e[1;36m[CLI ACTIVE]\e[0m"
    echo -e "\e[1;36m[CLI TRAFFIC]\e[0m"
    echo -e "\e[1;36m[NETWORK PULSE]\e[0m"
    for i in $(seq 0 7); do
        echo -e "    \e[1;34mServer ──> CLI${i}\e[0m"
    done
    echo -e "\e[1;36m[LOG SIZE]\e[0m"
    echo -e "\e[1;35m[CPU]\e[0m"
    echo -e "\e[1;35m[MEM]\e[0m"
    echo -e "\e[1;35m=======================================================\e[0m"
}

put() {
    local row=$1 col=$2
    shift 2
    printf "\033[%d;%dH%b\033[K" "$row" "$col" "$*"
}

draw_static

cpu_prev=0
mem_prev=0

while true; do
    mapfile -t pids < <(pgrep -x vepc 2>/dev/null)
    if [ ${#pids[@]} -gt 0 ]; then
        server_val="\e[1;32mРаботает (${#pids[@]} процессов)\e[0m"
        cpu_total=0; mem_total=0
        for pid in "${pids[@]}"; do
            c=$(ps -p "$pid" -o %cpu= 2>/dev/null | awk '{printf "%d", int($1+0.5)}')
            m=$(ps -p "$pid" -o %mem= 2>/dev/null | awk '{printf "%d", int($1+0.5)}')
            c=${c:-0}; m=${m:-0}
            cpu_total=$(( cpu_total + c ))
            mem_total=$(( mem_total + m ))
        done
    else
        server_val="\e[1;31mНЕ РАБОТАЕТ\e[0m"
        cpu_total=0; mem_total=0
    fi

    cpu=$(( (cpu_total + cpu_prev * 2) / 3 ))
    mem=$(( (mem_total + mem_prev * 2) / 3 ))
    cpu_prev=$cpu
    mem_prev=$mem

    # Безопасный подсчёт активных CLI-подключений
    if [ -S "$SOCKET_PATH" ]; then
        active_cli=$(lsof -U 2>/dev/null | grep -cF "$(basename "$SOCKET_PATH")" || true)
    else
        active_cli=0
    fi
    active_cli=$(( active_cli + 0 ))

    cli_bar=""
    for ((i=0; i<active_cli && i<8; i++)); do
        level=$(( RANDOM % 100 + 1 ))
        cli_bar+="$(progress_bar "$level" 100 5) "
    done

    log_size=$(stat -c%s "$LOG_FILE" 2>/dev/null || echo 0)
    log_size=$(( log_size + 0 ))
    log_bar=$(progress_bar "$log_size" "$MAX_LOG_SIZE" 30)
    cpu_bar=$(progress_bar "$cpu" 100 30)
    mem_bar=$(progress_bar "$mem" 100 30)

    tput sc

    put 2  10 "${server_val}"
    put 3  14 "\e[0;37m${active_cli}\e[0m"
    put 4  15 "\e[0;37m${cli_bar}\e[0m"

    for ((i=0; i<8; i++)); do
        row=$(( 6 + i ))
        if [ $i -lt "$active_cli" ]; then
            level=$(( RANDOM % 100 + 1 ))
            if [ $level -gt 50 ]; then sym="\e[1;32m⚡\e[0m"
            else sym="\e[1;34m▸\e[0m"; fi
        else
            sym="\e[0;90m─\e[0m"
        fi
        put "$row" 22 "${sym}"
    done

    put 14 12 "\e[0;37m$(( log_size / 1024 )) KB ${log_bar}\e[0m"
    put 15  7 "\e[0;37m${cpu}% ${cpu_bar}\e[0m"
    put 16  7 "\e[0;37m${mem}% ${mem_bar}\e[0m"

    tput rc
    sleep 1
done

#!/bin/bash
# setup.sh — очистка дублей и миграция структуры проекта
# Запускать один раз из папки проекта

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

echo -e "\e[1;35m===== Очистка структуры проекта =====\e[0m"
echo ""

# -------------------
# 1. Папки
# -------------------
mkdir -p config build/logs
echo -e "\e[1;32m[OK]\e[0m    Папки config/ и build/logs/ готовы"

# -------------------
# 2. Конфиги → config/
# -------------------
for f in interfaces.conf vmme.conf vsgsn.conf vepc.config; do
    if [ -f "$f" ]; then
        mv "$f" config/
        echo -e "\e[1;33m[MOVE]\e[0m  $f → config/$f"
    fi
done

# -------------------
# 3. Удаляем дубли C++ файлов
# -------------------
# src/cli.cpp — дубль CLI сервера, вся логика в main.cpp
if [ -f "src/cli.cpp" ]; then
    rm "src/cli.cpp"
    echo -e "\e[1;31m[DEL]\e[0m   src/cli.cpp (дубль CLI)"
fi

# cli/cli_server.cpp — дубль, не подключён к сборке
if [ -f "cli/cli_server.cpp" ]; then
    rm "cli/cli_server.cpp"
    echo -e "\e[1;31m[DEL]\e[0m   cli/cli_server.cpp (дубль)"
fi

# cli/cli_commands.cpp + .h — дубль обработки команд
if [ -f "cli/cli_commands.cpp" ]; then
    rm "cli/cli_commands.cpp"
    echo -e "\e[1;31m[DEL]\e[0m   cli/cli_commands.cpp (дубль)"
fi
if [ -f "cli/cli_commands.h" ]; then
    rm "cli/cli_commands.h"
    echo -e "\e[1;31m[DEL]\e[0m   cli/cli_commands.h (дубль)"
fi

# vepc-cli.sh — дубль panel_cli.sh
if [ -f "vepc-cli.sh" ]; then
    rm "vepc-cli.sh"
    echo -e "\e[1;31m[DEL]\e[0m   vepc-cli.sh (дубль panel_cli.sh)"
fi

# logs/ в корне — дубль build/logs/
if [ -d "logs" ]; then
    if [ -n "$(ls -A logs/ 2>/dev/null)" ]; then
        cp -n logs/* build/logs/ 2>/dev/null
        echo -e "\e[1;33m[MERGE]\e[0m logs/ → build/logs/"
    fi
    rm -rf logs/
    echo -e "\e[1;31m[DEL]\e[0m   logs/ (дубль, данные перенесены в build/logs/)"
fi

# -------------------
# 4. Права на скрипты
# -------------------
chmod +x monitoring.sh panel_*.sh 2>/dev/null
echo -e "\e[1;32m[OK]\e[0m    chmod +x *.sh"

echo ""
echo -e "\e[1;32m✓ Готово. Финальная структура:\e[0m"
echo ""
find . -not -path './.git/*' -not -path './build/*' | sort | grep -v '^\.$'

echo ""
echo -e "\e[1;33mДалее пересобери проект:\e[0m"
echo "  cd build && cmake .. && make && cd .."
echo ""
echo -e "\e[1;33mЗатем запускай:\e[0m"
echo "  ./monitoring.sh"

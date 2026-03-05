# Makefile для vEPC — простой, надёжный, без проблем с clock skew

.PHONY: all build clean rebuild run rerun debug deps help

# Основные переменные
TARGET = vepc
BUILD_DIR = build

# Основная цель — сборка и запуск
all: build run

# Сборка через CMake
build:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && make -j$(nproc)

# Очистка сборки (удаляет build полностью)
clean:
	@rm -rf $(BUILD_DIR)

# Полная пересборка с нуля
rebuild: clean build

# Сборка + запуск
run: build
	@./$(BUILD_DIR)/$(TARGET)

# Полная пересборка + запуск
rerun: rebuild run

# Запуск под отладчиком gdb
debug: build
	@gdb ./$(BUILD_DIR)/$(TARGET)

# Установка зависимостей (если нужно)
deps:
	@sudo apt update
	@sudo apt install -y build-essential cmake g++ gdb

# Помощь
help:
	@echo "Доступные команды:"
	@echo "  make          — собрать и запустить"
	@echo "  make build    — только собрать"
	@echo "  make clean    — удалить build"
	@echo "  make rebuild  — полная пересборка"
	@echo "  make run      — собрать и запустить"
	@echo "  make rerun    — пересобрать и запустить"
	@echo "  make debug    — запустить под gdb"
	@echo "  make deps     — установить зависимости"
	@echo "  make help     — показать эту справку"

# Запрет параллельного выполнения некоторых целей (чтобы избежать clock skew)
.NOTPARALLEL: clean rebuild

MAKEFLAGS += --no-print-directory
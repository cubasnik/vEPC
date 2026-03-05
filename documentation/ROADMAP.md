# vEPC — Virtual EPC (MME + SGSN)

## Цель проекта

Цели:
- Управление UE/SGSN/MME в одном процессе
- Реальные протоколы: GTP-C, S1AP, NAS
- Конфигурация через файлы и консоль
- Сохранение состояния (PDP/UE контексты)
- Цветные логи, файл логов, команда show/set/restart/state
- Возможность расширения до SGW/PGW/HSS 

## Принцип работы (общая архитектура)

1. **Единый процесс vepc**
   - Запускает несколько потоков: MME, SGSN, GTP-сервер, S1AP-сервер, консоль
   - Все потоки общаются через общие структуры (config, pdpContexts, ueContexts)

2. **Конфигурация**
   - Файлы: `vmme.conf`, `vsgsn.conf`
   - Читаются при запуске и перезапуске
   - Можно менять в реальном времени командой `set key value`

3. **Состояние**
   - PDP контексты (SGSN): TEID, IMSI, GGSN IP, PDP type
   - UE контексты (MME): IMSI, GUTI, authenticated flag, security context
   - Хранятся в std::map, защищены mutex

4. **Протоколы**
   - GTP-C (UDP 2123): Echo, Create PDP Context Request/Response
   - S1AP (SCTP 36412): Initial UE Message, Downlink NAS Transport
   - NAS: Authentication Request / Response (внутри S1AP)

5. **Консольное управление**
   - Команды: status, logs, state, show, set <key> <value>, restart, stop
   - Цветные логи в консоли + файл logs/vepc.log

## ROADMAP (этапы реализации)

### Этап 0 — База (сделано)
- Запуск MME/SGSN в отдельных потоках
- Чтение конфигов (../vmme.conf, ../vsgsn.conf)
- Команды: status, logs, state, show, set, restart, stop
- Цветные логи + файл логов
- Простые серверы-заглушки: GTP-C (UDP 2123), S1AP (SCTP 36412)

### Этап 1 — Реальный GTP-C (SGSN) — в работе
- Приём и парсинг GTPv1-C заголовка (version, type, length, TEID, sequence)
- Полная обработка:
  - Echo Request → Echo Response
  - Create PDP Context Request → Create PDP Context Response
- Извлечение ключевых полей: IMSI, APN, PDP type, GGSN IP
- Сохранение реального PDP контекста
- Команда `state` показывает реальные данные

### Этап 2 — Реальный S1AP + NAS (MME) — следующий шаг
- Полноценный SCTP-сервер (библиотека libsctp-dev)
- Парсинг S1AP: Initial UE Message (Procedure Code 12)
- Извлечение NAS PDU из S1AP
- Отправка Authentication Request (NAS type 0x52)
- Приём Authentication Response
- Сохранение UE контекста (IMSI, GUTI, security keys)
- Команда `state` показывает UE контексты

### Этап 3 — Сохранение состояния
- Сериализация PDP/UE контекстов в JSON или бинарный файл (при stop/restart)
- Загрузка при запуске (опционально)

### Этап 4 — GUI
- Вариант 1: улучшенный консольный интерфейс (таблицы, цвета, стрелки)
- Вариант 2: Qt GUI (окно логов, таблица контекстов, кнопки set/restart)

### Этап 5 — Расширение
- GTP-U (UDP 2152) — туннелирование пользовательских данных
- S11 — GTP-C к SGW
- S6a — Diameter к HSS  
- Поддержка handover (S3/S16 между SGSN и MME)

## Текущие файлы проекта

- CMakeLists.txt
- main.cpp
- vmme.conf
- vsgsn.conf
- interfaces.conf (опционально)
- logs/ (создаётся автоматически)
- logs/vepc.log

## Как запустить

```bash
cd /mnt/c/Users/Alexey/Desktop/min/vNE/vEPC
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)
./vepc
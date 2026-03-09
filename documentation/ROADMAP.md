# vEPC — Virtual EPC (MME + SGSN)

## Цель проекта

Цели:

- Управление UE/SGSN/MME в одном процессе
- Реальные протоколы: GTP-C, S1AP, NAS
- Конфигурация через файлы и консоль
- Сохранение состояния (PDP/UE контексты)
- Цветные логи, файл логов, команды show/set/restart/state и структурированный CLI для runtime-управления
- Возможность расширения до SGW/PGW/HSS

## Принцип работы (общая архитектура)

1. **Единый процесс vepc**
   - Запускает несколько потоков: MME, SGSN, GTP-сервер, S1AP-сервер, консоль
   - Все потоки общаются через общие структуры (config, pdpContexts, ueContexts)

2. **Конфигурация**
   - Файлы: `config/vepc.config`, `config/vmme.conf`, `config/vsgsn.conf`, `config/interfaces.conf`, `config/interface_admin_state.conf`
   - Runtime-конфиг собирается из `config/vepc.config`, `config/vmme.conf` и `config/vsgsn.conf`
   - Формат конфигов плоский: одна настройка на строку в виде `key = value`
   - Для MME/SGSN используются ключи с префиксами секций, например `s1ap-bind-ip`, `s6a-bind-port`, `gn-gtp-c-port`
   - Конфиги читаются при запуске и по команде `restart` без ручного перезапуска процесса
   - Команда `set key value` сохраняет общие ключи в `config/vepc.config`, MME-ключи в `config/vmme.conf`, SGSN-ключи в `config/vsgsn.conf`
   - Административное состояние интерфейсов сохраняется отдельно в `config/interface_admin_state.conf` и автоматически перечитывается после `restart`

3. **Состояние**
   - PDP контексты (SGSN): TEID, IMSI, GGSN IP, PDP type
   - UE контексты (MME): IMSI, GUTI, authenticated flag, security context
   - Хранятся в `std::map`, защищены mutex

4. **Протоколы**
   - GTP-C (UDP 2123): Echo, Create PDP Context Request/Response
   - S1AP (SCTP 36412): Initial UE Message, Downlink NAS Transport
   - NAS: Authentication Request / Response (внутри S1AP)

5. **Консольное управление**
   - Базовые команды: `status`, `logs`, `state`, `show`, `set key value`, `restart`, `stop`
   - CLI поддерживает также структурированный режим команд: `configure terminal`, `interface <name>`, `shutdown`, `no shutdown`, `show running-config`, `show logging`
   - `restart` возвращает расширенный статус и перечитывает конфиги без ручного рестарта процесса
   - Изменения по `iface_up`, `iface_down`, `iface_reset` и `shutdown`/`no shutdown` сохраняются автоматически без отдельного commit/save шага
   - Цветные логи в консоли + файл `logs/vepc.log`

## ROADMAP (этапы реализации)

### Executive Summary

- vEPC уже умеет поднимать единый runtime для MME/SGSN, читать конфиги и управляться через CLI без ручного редактирования файлов.
- Базовый операционный слой закрыт: есть `restart`, `show`, `set`, интерфейсные команды и цветной путь логирования.
- Интерфейсы перестали быть только статической таблицей: они имеют runtime-статус, реальную локальную проверку IP и переживают `restart` через `config/interface_admin_state.conf`.
- CLI вышел за пределы набора плоских команд и получил структурированные режимы для оператора.
- Текущее узкое место проекта уже не в интерфейсах и не в конфиге, а в протокольном пути для реального GTP-C и затем S1AP/NAS.
- Следующий самый ценный шаг для проекта: довести `Этап 0.6` до законченного операторского UX и сразу после этого закрывать реальный путь разбора GTP-C.
- До завершения `Этапа 1` и минимального `Этапа 2` не стоит распыляться на GUI или широкие интеграции.
- Слой persistence для интерфейсов уже есть; полноценное сохранение состояния нужно переносить на PDP/UE contexts, а не переизобретать текущий interface layer.
- Для демонстрации и разработки проект уже пригоден как управляемый runtime-макет EPC-узла.
- Для превращения в более реалистичный стенд нужен воспроизводимый сетевой сценарий: GTP-C Echo/Create PDP, затем минимальный успешный сценарий S1AP/NAS.

### Сводная таблица этапов

| Этап | Статус | Приоритет | Ключевой результат | Блокер перехода дальше |
| ------ | -------- | ----------- | -------------------- | ------------------------ |
| 0 | Сделано | P0 | Runtime, конфиги, базовые команды, restart | Нет |
| 0.5 | Сделано | P0 | Управляемые интерфейсы и сохранение admin-state | Нет |
| 0.6 | Сделано | P0 | Структурированный CLI и устойчивое операционное управление | Нет |
| 1 | В работе | P1 | Реальный путь разбора GTP-C и PDP context | Нужны формализованные структуры и повторяемый локальный тест |
| 2 | В работе | P1 | Минимальный поток S1AP/NAS и UE context | Нужны стабильный SCTP path и минимальная модель UE context |
| 3 | Сделано | P2 | Сохранение PDP/UE context | Нет |
| 4 | Не начато | P3 | GUI/расширенный UI | Не должен забирать фокус до завершения протокольного пути |
| 5 | В работе | P2 | GTP-U, S11, S6a, handover | Нужен минимальный наблюдаемый runtime path для каждого нового интерфейса |

### Правила приоритизации

- `P0` — критично для управляемого runtime и демонстрации базового сценария MME/SGSN
- `P1` — следующий функциональный слой, без которого проект остаётся только эмулятором интерфейсов и конфигов
- `P2` — важное расширение и удобство эксплуатации
- `P3` — долгосрочные улучшения и UI-слой

### Критерии готовности этапов

- Код собирается в Windows через `cmake --build build-win --config Release`
- Функция проверяется коротким CLI smoke test без ручной правки бинарников
- Изменения отражены в runtime-командах, логах или конфиге
- Для каждого завершённого пункта понятен артефакт: команда, файл состояния или наблюдаемое сетевое поведение

### Этап 0 — База (сделано)

Приоритет: `P0`

- Запуск MME/SGSN в отдельных потоках
- Загрузка runtime-конфига из `config/vepc.config`, `config/vmme.conf`, `config/vsgsn.conf`
- Команды: `status`, `logs`, `state`, `show`, `show iface`, `set`, `restart`, `stop`
- `set` сохраняет изменения обратно в соответствующий конфиг-файл
- `restart` перечитывает конфиги и поднимает сервис заново без ручного перезапуска процесса
- Цветные логи + файл логов
- Простые серверы-заглушки: GTP-C (UDP 2123), S1AP (SCTP 36412)

### Этап 0.5 — Интерфейсы (сделано)

Приоритет: `P0`

- Загрузка интерфейсов из `config/interfaces.conf`
- Команда `show iface` показывает сводный статус интерфейсов
- Команды `iface_status`, `iface_up`, `iface_down`, `iface_reset` управляют и отображают состояние интерфейсов
- Для локальных IP-адресов используется реальное определение адресов сетевых адаптеров ОС
- Реализован runtime-статус для `S1-MME`, `S6a`, `S13`, `S11`, `S10`, `S3`, `Gn`, `Gr`, `Gb`, `Gp`, `Gf`
- Административное состояние интерфейсов переживает `restart` через `config/interface_admin_state.conf`
- Разрешение путей к конфигам работает как из корня проекта, так и из `build-win/Release`

Критерии завершения:

- `iface_down <name> -> restart -> iface_status <name>` сохраняет `Admin State`
- `iface_reset <name>` удаляет runtime override и очищает `config/interface_admin_state.conf`
- `show iface` и `show interface <name>` возвращают согласованный статус

### Этап 0.6 — CLI и операционное управление (сделано)

Приоритет: `P0`

- CLI поддерживает структурированные режимы `vepc#`, `vepc(config)#`, `vepc(config-if-<name>)#`
- Поддержаны команды `configure terminal`, `interface <name>`, `shutdown`, `no shutdown`, `default`, `end`, `exit`
- Поддержаны обзорные команды `show running-config`, `show logging`, `show interface <name> [detail]`
- Команда `save` и алиас `write memory` распознаются как информационный no-op, потому что runtime-изменения применяются и сохраняются сразу
- После `restart` CLI повторно подключается к серверу с коротким retry вместо немедленного ошибки-соединения
- Help по режимам, structured runtime-команды и reconnect smoke tests реализованы и проверены

Ближайшие задачи:

- Перевести `status`, `state`, `set`, `restart`, `stop` в единый структурированный CLI без поломки старых алиасов
- Добавить единый `?`/`help` по режимам вместо только общей таблицы
- Выровнять имена команд `show iface` и `show interface` как один официальный путь, сохранив старый алиас
- Подготовить smoke test сценарии для CLI после `restart` и для interface-mode

Критерии завершения:

- Оператор может пройти полный сценарий `configure terminal -> interface Gn -> shutdown -> end -> show interface Gn detail`
- После `restart` CLI не требует ручного повторного запуска для типового сценария управления
- Help в каждом режиме показывает только допустимые команды текущего контекста

### Этап 1 — Реальный GTP-C (SGSN) — в работе

Приоритет: `P1`

- Приём и парсинг GTPv1-C заголовка (version, type, length, TEID, sequence)
- Полная обработка:
  - Echo Request → Echo Response
  - Create PDP Context Request → Create PDP Context Response
- Извлечение ключевых полей: IMSI, APN, PDP type, GGSN IP
- Сохранение реального PDP контекста
- Команда `state` показывает реальные данные

Ближайшие задачи:

- Формализовать структуру PDP context в runtime и логе
- Зафиксировать минимальный тестовый пакет GTP-C для Echo и Create PDP
- Разделить stub-ответы и реальный путь разбора, чтобы отладка не ломала демонстрационный режим

Критерии завершения:

- Полученный GTP-C пакет разбирается в структуру, а не только логируется как raw message
- `state` показывает как минимум IMSI, APN, PDP type, TEID, peer IP
- Echo и Create PDP проходят повторяемый локальный тест без ручного патча кода

### Этап 2 — Реальный S1AP + NAS (MME) — следующий шаг

Приоритет: `P1`

- Полноценный SCTP-сервер (библиотека `libsctp-dev`)
- Парсинг S1AP: Initial UE Message (Procedure Code 12)
- Извлечение NAS PDU из S1AP
- Отправка Authentication Request (NAS type `0x52`)
- Приём Authentication Response
- Сохранение UE контекста (IMSI, GUTI, security keys)
- Команда `state` показывает UE контексты

Риски и зависимости:

- Нужен воспроизводимый SCTP runtime path и тестовая среда для Windows/Linux
- Без чёткого формата UE context этап легко расползается в набор несвязанных parser-веток
- Понадобится определить минимальный поддерживаемый NAS flow до перехода к расширению процедур

Критерии завершения:

- MME принимает Initial UE Message и извлекает NAS payload
- Authentication Request и Authentication Response проходят один минимальный успешный сценарий
- В `state` появляется отдельный UE context block с IMSI/GUTI/authenticated

Начальные шаги этапа 2:

1. Шаг 2.1: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`, `CMakeLists.txt`
   - Изменения:
      - расширить runtime `UEContext`, чтобы он держал `IMSI`, `GUTI`, peer id, последний S1AP/NAS тип и timestamp
      - вынести demo parser `Initial UE Message` в отдельный модуль без привязки к transport layer
      - добавить smoke test на разбор demo S1AP/NAS пакета и стабильность `Authentication Request` bytes
      - показать детальные UE blocks в `state`, чтобы слой состояния был готов к runtime integration
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - у этапа 2 появляется безопасная точка входа, аналогичная parser/test contour из этапа 1

2. Шаг 2.2: сделано
   - Файлы: `main.cpp`, при необходимости `documentation/ROADMAP.md`
   - Изменения:
      - подключить demo S1AP parser к живому runtime thread без попытки выдавать это за полноценный SCTP ASN.1 stack
      - принимать demo Initial UE Message по transport placeholder path и создавать `UEContext`
   - при Attach Request (`0x41`) отправлять demo `Downlink NAS Transport` с вложенным NAS `Authentication Request` (`0x52`)
      - при Authentication Response (`0x53`) отмечать UE context как authenticated
   - Проверка:
      - `cmake --build build-win --config Release`
      - локальный smoke test `demo Initial UE Message -> Authentication Request -> state`
   - Ожидаемый результат:
      - этап 2 перестаёт быть только parser/test preparation и получает первый живой UE runtime flow

3. Шаг 2.3: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить явные demo parsers для `Authentication Request`, `Authentication Response` и `Downlink NAS Transport`
      - расширить `UEContext` полями auth flow state и `security_context_id`, чтобы response валидировался не только по NAS type `0x53`
      - принимать `Authentication Response` только при наличии ожидаемого pending auth request и совпадающего `security_context_id`
      - отразить auth-request/auth-response/security-context состояние в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request(ksi) -> Authentication Response(ksi) -> state`
   - Ожидаемый результат:
      - auth flow получает минимальную state machine и становится пригодным для дальнейшего расширения NAS/security контекста

4. Шаг 2.4: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Security Mode Command (0x5D)` и `Security Mode Complete (0x5E)`
      - после успешного `Authentication Response` отправлять `Security Mode Command` с тем же `security_context_id`
      - принимать `Security Mode Complete` только при наличии ожидаемого pending security mode command и совпадающего `security_context_id`
      - отразить selected NAS algorithm и security-mode progress в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> state`
   - Ожидаемый результат:
      - UE flow получает второй завершённый NAS/security шаг и становится ближе к последовательному attach demo path

5. Шаг 2.5: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Attach Accept (0x42)` и `Attach Complete (0x43)`
      - после успешного `Security Mode Complete` отправлять `Attach Accept` с тем же `security_context_id`
      - принимать `Attach Complete` только при наличии ожидаемого pending attach accept и завершённого security mode
      - отразить attach progress и финальное `attached` состояние в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> state`
   - Ожидаемый результат:
      - demo UE flow получает минимальный завершённый attach-path до финального attached state

6. Шаг 2.6: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Service Request (0x4C)` и `Service Accept (0x4D)`
      - после успешного `Attach Complete` принимать `Service Request` только для уже attached UE и отвечать `Service Accept`
      - отразить post-attach service состояние и default bearer id в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> state`
   - Ожидаемый результат:
      - demo UE flow получает минимальный post-attach service layer поверх attached state

7. Шаг 2.7: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Service Release Request (0x4E)` и `Service Release Complete (0x4F)`
      - после успешного `Service Accept` принимать `Service Release Request` только для UE в `service-active` и отвечать `Service Release Complete`
      - переводить UE из `service-active` обратно в attached-idle без потери attach/security context
      - отразить release progress в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> Service Release Request -> Service Release Complete -> state`
   - Ожидаемый результат:
      - demo UE flow получает минимальный возврат из active service в attached-idle state

8. Шаг 2.8: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Detach Request (0x45)` и `Detach Accept (0x46)`
      - принимать `Detach Request` только для UE в attached state и отвечать `Detach Accept`
      - после detach очищать attach/security/service runtime flags и переводить UE context в `detached`
      - отразить detach progress в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> Service Release Request -> Service Release Complete -> Detach Request -> Detach Accept -> state`
   - Ожидаемый результат:
      - demo UE flow получает минимальный attach teardown path и возвращается в detached state

9. Шаг 2.9: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Tracking Area Update Request (0x48)` и `Tracking Area Update Accept (0x49)`
      - принимать `Tracking Area Update Request` только для UE в attached-idle и отвечать `Tracking Area Update Accept`
      - хранить последний tracking area code и TAU progress в `state`
      - сохранить возможность последующего detach после успешного TAU
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> Service Release Request -> Service Release Complete -> Tracking Area Update Request -> Tracking Area Update Accept -> state`
   - Ожидаемый результат:
      - demo UE flow получает минимальный idle mobility transition между service release и detach

10. Шаг 2.10: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parser для `Tracking Area Update Complete (0x4A)`
      - после `Tracking Area Update Accept` принимать `Tracking Area Update Complete` только при pending TAU accept и совпадающем `security_context_id`
      - переводить UE из pending TAU в устойчивое updated-idle состояние без потери attach/security context и tracking area code
      - отразить TAU completion progress в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> Service Release Request -> Service Release Complete -> Tracking Area Update Request -> Tracking Area Update Accept -> Tracking Area Update Complete -> state`
   - Ожидаемый результат:
      - demo UE flow получает завершённый idle mobility path, после которого возможны как новый service request, так и detach

11. Шаг 2.11: сделано
   - Файлы: `main.cpp`, `src/s1ap_parser.h`, `src/s1ap_parser.cpp`, `test_s1ap_parser.cpp`
   - Изменения:
      - добавить demo parsers и builder для `Service Resume Request (0x50)` и `Service Resume Accept (0x51)`
      - после `Tracking Area Update Complete` принимать `Service Resume Request` только для UE в `tau-updated` и отвечать `Service Resume Accept`
      - переводить UE из updated-idle обратно в `service-active`, сохраняя tracking area code и security context
      - отразить service resume progress в `state`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальный smoke test `Attach Request -> Authentication Request -> Authentication Response -> Security Mode Command -> Security Mode Complete -> Attach Accept -> Attach Complete -> Service Request -> Service Accept -> Service Release Request -> Service Release Complete -> Tracking Area Update Request -> Tracking Area Update Accept -> Tracking Area Update Complete -> Service Resume Request -> Service Resume Accept -> state`
   - Ожидаемый результат:
      - demo UE flow получает явный возврат из updated-idle в service-active после mobility update

### Этап 3 — Сохранение состояния

Приоритет: `P2`

1. Шаг 3.1: сделано
   - Файлы: `main.cpp`
   - Изменения:
      - добавить минимальную JSON-сериализацию PDP/UE runtime contexts в `build/state/runtime_state.json`
      - вызывать сохранение при `stop()` и `restart()` как подготовительный шаг перед полной загрузкой состояния
      - зафиксировать в файле служебные поля `schema_version` и `saved_at`, чтобы следующий шаг загрузки опирался на стабильный формат
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальная проверка `Attach Request -> restart -> build/state/runtime_state.json`
   - Ожидаемый результат:
      - controlled `stop`/`restart` сохраняет диагностируемый JSON snapshot текущих PDP/UE contexts даже до реализации загрузки с диска

2. Шаг 3.2: сделано
   - Файлы: `main.cpp`
   - Изменения:
      - очищать in-memory `ueContexts` и `pdpContexts` перед загрузкой runtime-state при `start()`
      - загружать сохранённый JSON из `build/state/runtime_state.json` при запуске и после `restart()`
      - восстанавливать минимально достаточные поля UE/PDP context из файла, сохраняя диагностируемость через `state`
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальная проверка `удалить runtime_state.json -> start -> Attach Request -> restart -> state`
   - Ожидаемый результат:
      - runtime restart больше не зависит от старых in-memory context и поднимает PDP/UE state из последнего сохранённого JSON snapshot

3. Шаг 3.3: сделано
   - Файлы: `main.cpp`
   - Изменения:
      - валидировать `schema_version`, `saved_at`, `ue_contexts` и `pdp_contexts` при загрузке `runtime_state.json`
      - расширить JSON snapshot служебной секцией `metadata` и массивом `interface_admin_state`
      - восстанавливать persisted `interface_admin_state` вместе с UE/PDP contexts при `start()` и `restart()`
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальная проверка `iface_down Gn -> Attach Request -> restart -> build/state/runtime_state.json -> state`
   - Ожидаемый результат:
      - runtime_state получает проверяемую версионированную схему и сохраняет не только UE/PDP state, но и административный interface snapshot для более полной диагностики

4. Шаг 3.4: сделано
   - Файлы: `main.cpp`, `.gitignore`
   - Изменения:
      - добавить дополнительную валидацию согласованности загружаемых UE/PDP context, чтобы битый snapshot отбрасывался до частичного восстановления
      - при ошибке загрузки переносить поврежденный `build/state/runtime_state.json` в quarantine-файл `runtime_state.corrupt-<timestamp>.json`
      - исключить `config/interface_admin_state.conf` из git, чтобы runtime admin-state не создавал шум в рабочем дереве
   - добавить отдельный CTest smoke `runtime-state-recovery-smoke` для автоматической проверки quarantine recovery
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release --output-on-failure`
      - локальная проверка `подложить битый runtime_state.json -> start -> build/state/runtime_state.corrupt-*.json`
   - Ожидаемый результат:
      - старт не застревает на поврежденном state-файле, переносит его в quarantine и продолжает работу с пустым runtime-state

5. Шаг 3.5: сделано
   - Файлы: `main.cpp`, `cmake/TestRuntimeStateRecovery.cmake`
   - Изменения:
      - автоматически удерживать только последние quarantine snapshots `runtime_state.corrupt-*.json`, чтобы recovery-path не засорял `build/state`
      - расширить `runtime-state-recovery-smoke`, чтобы он проверял не только перенос битого state-файла, но и pruning старых quarantine copies
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release -R runtime-state-recovery-smoke --output-on-failure`
   - Ожидаемый результат:
      - quarantine recovery остаётся диагностируемым, но не накапливает бесконечный хвост устаревших corrupt snapshots

6. Шаг 3.6: сделано
   - Файлы: `CMakeLists.txt`, `cmake/TestRuntimeStateRestore.cmake`, `test_runtime_cli.cpp`
   - Изменения:
      - добавить отдельный CTest smoke `runtime-state-restore-smoke`, который поднимает `vepc` на валидном snapshot и проверяет восстановленный `state` через сырой TCP CLI helper
      - зафиксировать позитивный restore path для UE/PDP contexts, чтобы persistence проверялась не только на corrupt recovery, но и на успешную загрузку
   - Проверка:
      - `cmake --build build-win --config Release --target test-runtime-cli vepc vepc-cli`
      - `ctest --test-dir build-win -C Release -R runtime-state-restore-smoke --output-on-failure`
   - Ожидаемый результат:
      - restart/start path воспроизводимо восстанавливает сохранённые UE/PDP contexts и делает это наблюдаемым через `state`

- Сериализация PDP/UE контекстов в JSON или бинарный файл (при `stop`/`restart`)
- Загрузка при запуске (опционально)
- Runtime-состояние интерфейсов уже сохраняется отдельно; следующий шаг здесь именно для PDP/UE-контекстов

Решение для этапа:

- Предпочтительный формат: JSON для читаемости и диагностики на раннем этапе
- Бинарный формат имеет смысл только после стабилизации схемы PDP/UE context

Критерии завершения:

- `stop` или `restart` сохраняет PDP/UE contexts в отдельный state file
- После запуска state восстанавливается без дублирования и повреждения записей
- Формат файла документирован и пригоден для ручной диагностики

### Этап 4 — GUI

Приоритет: `P3`

- Вариант 1: улучшенный консольный интерфейс (таблицы, цвета, стрелки)
- Вариант 2: Qt GUI (окно логов, таблица контекстов, кнопки `set`/`restart`)

Замечание:

- До завершения этапов `1` и `2` GUI не должен оттягивать ресурсы от runtime и protocol path

### Этап 5 — Расширение

Приоритет: `P2`

- GTP-U (UDP 2152) — туннелирование пользовательских данных
- S11 — GTP-C к SGW
- S6a — Diameter к HSS
- Поддержка handover (S3/S16 между SGSN и MME)

Зависимости:

- Имеет смысл только после стабилизации базовых PDP/UE контекстов и управляемого state layer

1. Шаг 5.1: сделано
   - Файлы: `main.cpp`, `CMakeLists.txt`, `cmake/TestGtpuTelemetry.cmake`, `test_runtime_cli.cpp`
   - Изменения:
      - поднять минимальный наблюдаемый `GTP-U` runtime path поверх существующего generic endpoint thread без полноценной user-plane логики
      - использовать `gn-gtp-u-bind-ip` как effective bind для `Gn`, чтобы `GTP-U` endpoint реально поднимался на локальном адресе из `vsgsn.conf`
      - добавить endpoint telemetry в `state` и `iface_status`, включая `Rx Packets`, `Rx Bytes`, `Last Peer` и `Last Activity`
      - покрыть это CTest smoke `gtp-u-telemetry-smoke`, который шлёт UDP datagram на `127.0.0.1:2152` и проверяет counters/logs
   - Проверка:
      - `cmake --build build-win --config Release --target test-runtime-cli vepc vepc-cli`
      - `ctest --test-dir build-win -C Release -R gtp-u-telemetry-smoke --output-on-failure`
   - Ожидаемый результат:
      - `Gn` перестаёт быть только конфиг-записью и получает минимально наблюдаемый runtime path, пригодный для дальнейшего наращивания `GTP-U`

2. Шаг 5.2: сделано
   - Файлы: `main.cpp`, `CMakeLists.txt`, `cmake/TestS11Telemetry.cmake`
   - Изменения:
      - перевести `S11` на generic endpoint thread вместо отдельного ownership через `GTP-C` server thread, чтобы telemetry и smoke использовали один и тот же runtime path
      - ограничить special-case для `s11-bind-ip` и `s11-port` только интерфейсом `S11`, чтобы не перехватывать другие UDP interfaces на порту `2123`
      - добавить smoke `s11-telemetry-smoke`, который шлёт UDP datagram на `127.0.0.1:2123` и проверяет `iface_status S11`, `state` и лог receive path
   - Проверка:
      - `cmake --build build-win --config Release --target vepc`
      - `ctest --test-dir build-win -C Release -R s11-telemetry-smoke --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает минимально наблюдаемый runtime path с `Rx Packets`, `Rx Bytes`, `Last Peer` и `Last Activity`, пригодный для следующего шага по реальному `GTP-C` handling

3. Шаг 5.3: сделано
   - Файлы: `main.cpp`, `CMakeLists.txt`, `cmake/TestS11Telemetry.cmake`, `test_s11_gtpc_client.cpp`
   - Изменения:
      - пропустить входящие UDP datagrams на `S11` через существующий `GTPv1-C` parser и `handleRealGtpMessage`, не возвращаясь к отдельному `GTP-C` server thread ownership
      - добавить helper `test-s11-gtpc-client`, который шлёт demo `Create PDP Context Request` и проверяет стабильный `Create PDP Context Response`
      - усилить `s11-telemetry-smoke`: очищать runtime state перед стартом, проверять создание `PDP context` через `state` и подтверждать parse/response logs для `Create PDP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R s11-telemetry-smoke --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает минимальный наблюдаемый `GTP-C` roundtrip path, в котором demo `Create PDP` не только доходит до сокета, но и приводит к обновлению `PDP contexts` и ответу в сеть

4. Шаг 5.4: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11DeletePdp.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Delete PDP Context Request/Response` path для `S11`, удаляющий `PDP context` по `TEID` и возвращающий стабильный demo response
      - расширить parser-level coverage для `Delete PDP Context` и научить `test-s11-gtpc-client` работать в режимах `create` и `delete`
      - добавить smoke `s11-delete-pdp-smoke`, который доказывает последовательность `Create PDP -> state=1 -> Delete PDP -> state=0` и проверяет соответствующие `GTP` logs
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "s11-(telemetry|delete-pdp)-smoke|gtp-parser-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает второй наблюдаемый `GTP-C` message type поверх уже работающего create-path и может не только создавать, но и удалять demo `PDP context`

5. Шаг 5.5: сделано
   - Файлы: `test_s11_gtpc_client.cpp`, `cmake/TestS11Echo.cmake`, `CMakeLists.txt`
   - Изменения:
      - расширить `test-s11-gtpc-client` режимом `echo`, который шлёт demo `Echo Request` и проверяет стабильный `Echo Response`
      - добавить smoke `s11-echo-smoke`, который доказывает `S11 Echo Request/Response` roundtrip, проверяет `iface_status S11`, подтверждает отсутствие побочных изменений в `PDP contexts` и валидирует `GTP` echo logs
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "s11-(echo|telemetry|delete-pdp)-smoke|gtp-parser-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает отдельный `GTP-C` liveness-path для `Echo Request/Response`, который наблюдаем через smoke-тесты и не меняет runtime state

6. Шаг 5.6: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11UpdatePdp.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Update PDP Context Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID` и отражает изменения в runtime state
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `update` с проверкой стабильного `Update PDP Context Response`
      - добавить smoke `s11-update-pdp-smoke`, который доказывает последовательность `Create PDP -> Update PDP -> state changed` и валидирует новые `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает следующий session-management message type поверх уже работающего create/delete path и может обновлять demo `PDP context`, а не только создавать или удалять его

7. Шаг 5.7: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11ActivatePdp.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Initiate PDP Context Activation Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `activate` с проверкой стабильного `Initiate PDP Context Activation Response`
      - добавить smoke `s11-activate-pdp-smoke`, который доказывает последовательность `Create PDP -> Initiate PDP Context Activation -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый session-management path поверх create/update/delete, в котором runtime state отражает фазу `Initiate PDP Context Activation`, а не только общий факт обновления контекста

8. Шаг 5.8: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11PduNotification.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `PDU Notification Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `notify` с проверкой стабильного `PDU Notification Response`
      - добавить smoke `s11-pdu-notification-smoke`, который доказывает последовательность `Create PDP -> PDU Notification -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|pdu-notification|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый PDP-related control path поверх create/update/delete/activate, в котором runtime state отражает фазу `PDU Notification`, а не только общий факт обновления контекста

9. Шаг 5.9: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11PduNotificationReject.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `PDU Notification Reject Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `reject` с проверкой стабильного `PDU Notification Reject Response`
      - добавить smoke `s11-pdu-notification-reject-smoke`, который доказывает последовательность `Create PDP -> PDU Notification Reject -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|pdu-notification|pdu-notification-reject|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый PDP-related control path поверх create/update/delete/activate/notify, в котором runtime state отражает фазу `PDU Notification Reject`, а не только общий факт обновления контекста

10. Шаг 5.10: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11FailureReport.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Failure Report Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `failure` с проверкой стабильного `Failure Report Response`
      - добавить smoke `s11-failure-report-smoke`, который доказывает последовательность `Create PDP -> Failure Report -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|failure-report|pdu-notification|pdu-notification-reject|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый control path поверх create/update/delete/activate/notify/reject, в котором runtime state отражает фазу `Failure Report`, а не только общий факт обновления контекста

11. Шаг 5.11: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11NoteMsPresent.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Note MS GPRS Present Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `present` с проверкой стабильного `Note MS GPRS Present Response`
      - добавить smoke `s11-note-ms-present-smoke`, который доказывает последовательность `Create PDP -> Note MS GPRS Present -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|note-ms-present|failure-report|pdu-notification|pdu-notification-reject|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый control path поверх create/update/delete/activate/notify/reject/failure, в котором runtime state отражает фазу `Note MS GPRS Present`, а не только общий факт обновления контекста

12. Шаг 5.12: сделано
   - Файлы: `main.cpp`, `src/gtp_parser.h`, `src/gtp_parser.cpp`, `test_gtp_parser.cpp`, `test_s11_gtpc_client.cpp`, `cmake/TestS11Identification.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `Identification Request/Response` path для `S11`, который обновляет существующий `PDP context` по `TEID`, фиксирует новый `Message Type` в runtime state и возвращает стабильный demo response
      - расширить parser-level coverage и helper `test-s11-gtpc-client` режимом `identify` с проверкой стабильного `Identification Response`
      - добавить smoke `s11-identification-smoke`, который доказывает последовательность `Create PDP -> Identification -> state changed` и валидирует новые `Message Type`, `APN`, `PDP Type` и `GGSN IP`
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "gtp-parser-smoke|s11-(telemetry|identification|note-ms-present|failure-report|pdu-notification|pdu-notification-reject|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S11` получает ещё один наблюдаемый control path поверх create/update/delete/activate/notify/reject/failure/present, в котором runtime state отражает фазу `Identification`, а не только общий факт обновления контекста

13. Шаг 5.13: сделано
   - Файлы: `main.cpp`, `src/diameter_parser.h`, `src/diameter_parser.cpp`, `test_diameter_parser.cpp`, `test_s6a_diameter_client.cpp`, `cmake/TestS6aTelemetry.cmake`, `CMakeLists.txt`
   - Изменения:
      - добавить минимальный `S6a` roundtrip path для `DIAMETER`: generic connection-oriented endpoint принимает TCP connection, читает `CER`, обновляет interface telemetry counters и возвращает demo `CEA` с минимальным AVP-набором
      - вынести минимальный Diameter header parser/builder с распознаванием `Capabilities-Exchange-Request/Answer`, разбором `Origin-Host` и `Origin-Realm` из входящего `CER` и формированием `CEA` с `Result-Code`, `Origin-Host` и `Origin-Realm` из текущего `S6a` конфига
      - добавить parser-level coverage, demo TCP client и smoke `s6a-telemetry-smoke`, который доказывает `S6a` bind, приём `CER`, возврат содержательного `CEA`, обновление `iface_status/state` с parsed AVP detail и наличие диагностических логов
   - Проверка:
      - `cmake --build build-win --config Release`
      - `ctest --test-dir build-win -C Release -R "diameter-parser-smoke|s6a-telemetry-smoke|gtp-u-telemetry-smoke|s11-(telemetry|identification|note-ms-present|failure-report|pdu-notification|pdu-notification-reject|activate-pdp|update-pdp|delete-pdp|echo)-smoke" --output-on-failure`
      - `ctest --test-dir build-win -C Release --output-on-failure`
   - Ожидаемый результат:
      - `S6a` перестаёт быть только placeholder listener и получает первый наблюдаемый runtime roundtrip для Diameter `CER/CEA` с минимально полезным AVP-содержимым и извлечением `Origin-Host`/`Origin-Realm` в runtime telemetry, не пытаясь пока эмулировать полный HSS workflow

## Ближайший план (рекомендуемый порядок)

1. Закрыть `Этап 0.6`: help по режимам, структурированные обёртки для всех runtime-команд, стабильные smoke tests.
2. Довести `Этап 1`: реальный путь разбора GTP-C и отображение PDP context в `state`.
3. Начать `Этап 2` только после фиксации минимального формата UE context и тестового успешного сценария для S1AP/NAS.
4. После этого переходить к `Этапу 3`, чтобы сохранять уже устоявшиеся структуры данных, а не временные формы.

## Рабочий Backlog

### Backlog — Этап 0.6

- `P0`: добавить help по режимам `exec/config/interface-config`, чтобы оператор видел только валидные команды текущего контекста
- `P0`: довести structured-аналоги для `status`, `state`, `restart`, `stop`, `set`
- `P0`: унифицировать `show iface` и `show interface` как один официальный путь с совместимостью по старому имени
- `P0`: добавить smoke test сценарий `restart -> reconnect -> interface change -> show interface detail`
- `P1`: добавить `?` как короткий алиас help в текущем режиме
- `P1`: отделить локальные CLI-подсказки от ответов сервера, чтобы help не смешивался с runtime-output

Статус выполнения:

- `0.6.1`: сделано
- `0.6.2`: сделано
- `0.6.3`: сделано
- `0.6.4`: сделано
- `0.6.5`: сделано

Критерии готовности:

- Оператор может работать без знания старых legacy-команд
- Любая типовая операция по интерфейсу выполняется через structured flow
- После `restart` CLI-сценарий остаётся воспроизводимым без ручного вмешательства

План реализации:

1. Шаг 0.6.1: help по режимам
    - Файлы: `cli/vepc-cli.cpp`
    - Изменения:
       - выделить отдельные функции `printExecHelp()`, `printConfigHelp()`, `printInterfaceHelp()`
       - переключать help по `CliMode`, а текущий `printHelp()` оставить как стартовую обзорную таблицу
       - добавить алиас `?` для вызова help текущего режима
    - Проверка:
       - `"?`nexit" | .\build-win\Release\vepc-cli.exe`
       - `"configure terminal`n?`ninterface Gn`n?`nend`nexit" | .\build-win\Release\vepc-cli.exe`
    - Ожидаемый результат:
       - в `exec` показываются только exec-команды
       - в `config` показываются только config-команды
       - в `interface-config` показываются только interface-команды

2. Шаг 0.6.2: structured-обёртки для runtime-команд
    - Файлы: `cli/vepc-cli.cpp`, при необходимости `main.cpp`
    - Изменения:
       - добавить распознавание structured-форм для `status`, `state`, `restart`, `stop`
       - для `set` определить безопасный синтаксис, например `set <key> <value>` в `exec` или `config` режиме
       - legacy-команды оставить как совместимый слой, а не как основной путь
    - Проверка:
       - `"status`nstate`nrestart`nexit" | .\build-win\Release\vepc-cli.exe`
       - `"configure terminal`nset mme-ip 127.0.0.1`nend`nshow running-config`nexit" | .\build-win\Release\vepc-cli.exe`
    - Ожидаемый результат:
       - основные runtime-команды выполняются без обращения к номерной таблице
       - оператор может использовать только текстовые structured-команды

3. Шаг 0.6.3: унификация `show iface` и `show interface`
    - Файлы: `cli/vepc-cli.cpp`, при необходимости `main.cpp`
    - Изменения:
       - сделать `show interface` основным пользовательским синтаксисом
       - оставить `show iface` как backward-compatible алиас
       - выровнять help, подсказки и ошибки usage под один основной путь
    - Проверка:
       - `"show interface`nshow interface Gn`nshow iface`nshow iface Gn`nexit" | .\build-win\Release\vepc-cli.exe`
    - Ожидаемый результат:
       - оба синтаксиса работают одинаково
       - в help основным отображается `show interface`

4. Шаг 0.6.4: smoke tests после `restart`
    - Файлы: `cli/vepc-cli.cpp`, `.vscode/tasks.json` при необходимости
    - Изменения:
       - закрепить короткие сценарии проверки reconnect path
       - при необходимости добавить task или документированный тестовый сценарий в roadmap
    - Проверка:
       - `"restart`nshow interface Gn detail`nexit" | .\build-win\Release\vepc-cli.exe`
       - `"configure terminal`ninterface Gn`nshutdown`nend`nrestart`nshow interface Gn detail`nexit" | .\build-win\Release\vepc-cli.exe`
    - Ожидаемый результат:
       - CLI после `restart` повторно подключается без ручного старта нового клиента
       - изменения интерфейса корректно видны после reconnect

5. Шаг 0.6.5: отделение локального UX от server output
    - Файлы: `cli/vepc-cli.cpp`
    - Изменения:
       - визуально и логически отделить локальные help/hint/warning сообщения от ответа сервера
       - не смешивать help-таблицы с runtime-ответом в одном блоке вывода
    - Проверка:
       - `"help`nshow running-config`nconfigure terminal`n?`nend`nexit" | .\build-win\Release\vepc-cli.exe`
    - Ожидаемый результат:
       - оператору легко отличить локальную подсказку CLI от ответа vepc server

Порядок выполнения по коду:

- сначала `0.6.1`, потому что mode-aware help определяет пользовательскую модель CLI
- затем `0.6.2`, потому что после появления режима нужно закрыть основные runtime-команды
- затем `0.6.3`, чтобы зафиксировать единый пользовательский синтаксис показа интерфейсов
- потом `0.6.4`, чтобы стабилизировать reconnect path и сценарии верификации
- последним `0.6.5`, как финальная полировка операторского UX

### Backlog — Этап 1

- `P0`: формализовать структуру PDP context в коде и в отображении `state`
- `P0`: выделить parser path для GTPv1-C header и message type
- `P0`: реализовать устойчивую обработку Echo Request/Response
- `P0`: реализовать минимальную обработку Create PDP Context Request/Response с заполнением runtime PDP context
- `P1`: добавить детальный лог разбора полей IMSI/APN/PDP type/GGSN IP
- `P1`: подготовить локальный тестовый набор пакетов или сценарий генерации GTP-C сообщений
- `P1`: развести demo stub path и real parser path по коду и логике флагов

Статус выполнения:

- `1.1`: сделано
- `1.2`: сделано
- `1.3`: сделано
- `1.4`: сделано
- `1.5`: сделано
- `1.6`: сделано

Критерии готовности:

- `state` показывает реальный PDP context, созданный из разобранного GTP-C пакета
- Echo и Create PDP работают повторяемо на локальном тестовом сценарии
- Ошибки парсинга диагностируются через лог, а не через молчаливый отказ

План реализации:

1. Шаг 1.1: формализация PDP context и отображения state
    - Файлы: `main.cpp`
    - Изменения:
       - проверить и при необходимости расширить `struct PDPContext`, чтобы в ней были минимум `TEID`, `IMSI`, `APN`, `PDP type`, `GGSN IP`, `timestamp/sequence`
       - привести `pdpContexts` к одной понятной схеме ключа, чтобы было ясно, индексируется ли контекст по `TEID`, `sequence` или внутреннему id
       - обновить `printState()` так, чтобы PDP context выводился в человекочитаемом виде как отдельный блок
    - Проверка:
       - локальный вызов `state` после создания тестового PDP context
    - Ожидаемый результат:
       - формат runtime state стабилен до начала полноценного parser path

2. Шаг 1.2: выделение GTPv1-C header parser path
    - Файлы: `main.cpp`
    - Изменения:
       - выделить отдельную функцию разбора GTPv1-C заголовка, например `parseGtpHeader(...)`
       - парсить минимум `version`, `flags`, `messageType`, `length`, `TEID`, `sequence`
       - добавить явную валидацию длины пакета до разбора AVP/IE-части
    - Проверка:
       - подать корректный и некорректный raw packet в parser helper
       - убедиться, что некорректный пакет уходит в лог с понятной причиной отказа
    - Ожидаемый результат:
       - сервер умеет отделять валидный GTP header от мусорного пакета

3. Шаг 1.3: Echo Request / Echo Response
    - Файлы: `main.cpp`, при необходимости новый тестовый файл `test_gtp_parser.cpp`
    - Изменения:
       - реализовать обработчик `Echo Request`
       - формировать корректный `Echo Response`
       - логировать ключевые поля запроса и факт отправки ответа
    - Проверка:
       - локальный тестовый пакет Echo Request
       - проверка по логам и по возвращаемому буферу ответа
    - Ожидаемый результат:
       - Echo path работает независимо от Create PDP и уже подтверждает жизнеспособность parser/response слоя

4. Шаг 1.4: минимальный Create PDP Context Request parser
    - Файлы: `main.cpp`, при необходимости новый тестовый файл `test_gtp_parser.cpp`
    - Изменения:
       - выделить минимальный набор IE, которые реально нужны для demo path: `IMSI`, `APN`, `PDP type`, `GGSN IP`
       - сделать parser устойчивым к отсутствию необязательных IE
       - при ошибке разбора не падать, а возвращать диагностируемый отказ
    - Проверка:
       - локальный тестовый пакет Create PDP Context Request
       - проверка логов на извлечённые значения полей
    - Ожидаемый результат:
       - Create PDP request даёт заполненную структуру запроса, пригодную для создания runtime context

5. Шаг 1.5: создание runtime PDP context и ответ Create PDP Context Response
    - Файлы: `main.cpp`
    - Изменения:
       - при успешном разборе Create PDP request сохранять контекст в `pdpContexts`
       - формировать минимальный успешный Create PDP response
       - обновлять `state`, чтобы новый контекст был виден сразу после обработки пакета
    - Проверка:
       - сценарий `Create PDP request -> state`
       - проверка, что в `state` видны `IMSI`, `APN`, `PDP type`, `TEID`, `GGSN IP`
    - Ожидаемый результат:
       - runtime context создаётся из реального входящего GTP-C сообщения, а не из заглушки

6. Шаг 1.6: тестовый контур и разделение demo/stub path
    - Файлы: `main.cpp`, `CMakeLists.txt`, при необходимости `test_gtp_parser.cpp`
    - Изменения:
       - вынести parser helpers так, чтобы их можно было тестировать без запуска всего `vepc`
       - развести старый stub path и новый real parser path через явную ветку кода
       - включить простой тест в сборку, если это не ломает текущий Windows flow
    - Проверка:
       - `cmake --build build-win --config Release`
       - запуск теста или документированного локального сценария генерации пакетов
    - Ожидаемый результат:
       - дальнейшая работа над GTP-C идёт через тестируемые функции, а не только через ручной runtime smoke test

Порядок выполнения по коду:

- сначала `1.1`, потому что без стабильной структуры PDP context нельзя осмысленно закрывать parser и `state`
- затем `1.2`, чтобы получить безопасный и диагностируемый вход в GTP-C слой
- потом `1.3`, потому что Echo является самым коротким и надёжным вертикальным срезом parser/response path
- затем `1.4` и `1.5`, чтобы построить минимальный рабочий Create PDP сценарий
- последним `1.6`, чтобы закрепить всё тестовым контуром и не деградировать обратно к ручной отладке

## Текущие файлы проекта

- `CMakeLists.txt`
- `main.cpp`
- `config/vepc.config`
- `config/vmme.conf`
- `config/vsgsn.conf`
- `config/interfaces.conf`
- `config/interface_admin_state.conf`
- `logs/` (создаётся автоматически)
- `logs/vepc.log`

## Формат конфигов

- Все актуальные конфиги используют единый плоский формат `key = value`
- `config/vepc.config` хранит общие runtime-параметры, например `mme-ip`, `sgsn-ip`, `s1ap-port`, `gtp-c-port`
- `config/vmme.conf` хранит MME-специфичные параметры, например `mme-name`, `s1ap-bind-ip`, `s6a-hss-host`
- `config/vsgsn.conf` хранит SGSN-специфичные параметры, например `gb-bind-ip`, `gn-gtp-u-port`, `iups-enabled`
- `config/interfaces.conf` хранит табличное описание интерфейсов в формате `Name | Protocol | IP:Port | RemoteNE`
- `config/interface_admin_state.conf` хранит только административные runtime-overrides интерфейсов (`up`/`down`), которые были изменены через CLI
- При загрузке MME/SGSN-специфичные ключи маппятся в runtime aliases вроде `s1ap-port`, `gtp-c-port`, `gtp-u-port`

## Как запустить

### Windows

```powershell
cd C:/Users/Alexey/Desktop/min/vNE/vEPC
cmake -S . -B build-win
cmake --build build-win --config Release
./build-win/Release/vepc.exe
```

CLI в отдельном терминале:

```powershell
cd C:/Users/Alexey/Desktop/min/vNE/vEPC
./build-win/Release/vepc-cli.exe
```

### Linux

```bash
cd /mnt/c/Users/Alexey/Desktop/min/vNE/vEPC
cmake -S . -B build
cmake --build build -j$(nproc)
./build/vepc
```

CLI в отдельном терминале:

```bash
cd /mnt/c/Users/Alexey/Desktop/min/vNE/vEPC
./build/vepc-cli
```

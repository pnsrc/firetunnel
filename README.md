# FireTunnel Qt Client

Графический клиент для TrustTunnel на Qt.

## Структура проекта

- `main.cpp` — точка входа приложения
- `src/ui/` — окно приложения и диалоги
- `src/core/` — настройки, инспекция и хранилище конфигов, UI-утилиты
- `src/vpn/` — Qt-обертка VPN клиента и helper
- `include/` — заголовки по тем же подсистемам (`ui`, `core`, `vpn`)
- `assets/` — иконки и ресурсы
- `docs/` — документация клиента

## Сборка

Рекомендуется собирать через локальный Makefile клиента (он вызывает корневой CMake):

```sh
make -C trusttunnel-qt build
```

Полезные параметры:

```sh
make -C trusttunnel-qt build BUILD_DIR=../build BUILD_TYPE=Debug JOBS=12
make -C trusttunnel-qt build CMAKE_PREFIX_PATH=/opt/homebrew/Cellar/qt/6.10.2
```

Запуск:

```sh
make -C trusttunnel-qt run
```

## Важное по зависимостям

Если Conan/Quiche падает на `boring` (yanked crates), собирайте без HTTP3:

```sh
make -C trusttunnel-qt build QT_DISABLE_HTTP3=ON
```

## Быстрый сценарий в UI

1. Откройте `Browse...` и выберите TOML конфиг.
2. Нажмите `Start VPN`.
3. Для остановки — `Stop VPN`.
4. В `Settings` можно включить автоподключение при старте приложения.

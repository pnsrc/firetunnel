# FireTunnel

Графический клиент TrustTunnel на Qt.

## Сборка

Рекомендуемый вариант: `trusttunnel-qt/Makefile`, который собирает через корневой CMake (чтобы корректно подтягивались Conan-зависимости).

```sh
make -C trusttunnel-qt build
make -C trusttunnel-qt run
```

Параметры:

```sh
make -C trusttunnel-qt build BUILD_DIR=build BUILD_TYPE=Debug JOBS=12
make -C trusttunnel-qt build CMAKE_PREFIX_PATH=/opt/homebrew/Cellar/qt/6.10.2
```

Альтернатива: ручная сборка из корня проекта:

```sh
cd .. # перейти в корень TrustTunnelClient
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH="/opt/homebrew/Cellar/qt/6.10.2" -DDISABLE_HTTP3=ON
make
./trusttunnel-qt/trusttunnel-qt
```

- Для macOS путь к Qt6 можно узнать через `brew info qt6` или с помощью Qt Maintenance Tool.
- Если возникает ошибка при сборке quiche/boring, добавьте опцию `-DDISABLE_HTTP3=ON` при вызове cmake (см. выше) — это отключит сборку HTTP/3 и позволит собрать остальной проект.

### Сборка через Makefile (корень проекта)

Из корня `TrustTunnelClient` доступны отдельные цели:

```sh
make build_qt_client_macos SKIP_BOOTSTRAP=1
make build_qt_client_linux SKIP_BOOTSTRAP=1
```

- `build_qt_client_macos` запускается только на macOS.
- `build_qt_client_linux` запускается только на Linux.
- Обе цели собирают `trusttunnel-qt` и `trusttunnel-qt-helper`.

## Запуск VPN из UI

1. Запустите `trusttunnel-qt`.
2. Выберите TOML-конфиг кнопкой `Browse...`.
3. Нажмите `Load config`.
4. Нажмите `Connect` для запуска VPN.
5. Нажмите `Disconnect` для остановки VPN.

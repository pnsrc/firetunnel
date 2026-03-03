#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ConfigInspector.h"
#include "ConfigStore.h"
#include "NetworkAdapterManager.h"
#include "vpn/trusttunnel/version.h"

#include <QTimer>

#ifndef FIRETUNNEL_VERSION
#define FIRETUNNEL_VERSION "dev"
#endif
#ifndef FIRETUNNEL_GIT_VERSION
#define FIRETUNNEL_GIT_VERSION "unknown"
#endif

SettingsDialog::SettingsDialog(const QString &lang, const AppSettings &settings, QWidget *parent) : QDialog(parent) {
    const bool ru = (lang == "ru");
    setWindowTitle("Settings");
    resize(680, 460);

    auto *layout = new QVBoxLayout(this);

    // Navigation: left list with icons, right stacked pages
    auto *bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    m_navList = new QListWidget(this);
    m_navList->setViewMode(QListView::ListMode);
    m_navList->setMovement(QListView::Static);
    m_navList->setFixedWidth(180);
    m_navList->setSpacing(4);
    m_stack = new QStackedWidget(this);

    auto addNavItem = [this](const QString &title, QWidget *page, const QIcon &icon) {
        auto *item = new QListWidgetItem(icon, title, m_navList);
        m_stack->addWidget(page);
        m_navList->addItem(item);
    };

    auto *aboutPage = new QWidget(this);
    auto *aboutLayout = new QFormLayout(aboutPage);
    const QString fireTunnelDetails = ru
            ? "Клиент: FireTunnel<br/>Разработчик: pnsrc<br/>GitHub: <a href=\"https://github.com/pnsrc\">https://github.com/pnsrc</a>"
            : "Client: FireTunnel<br/>Developer: pnsrc<br/>GitHub: <a href=\"https://github.com/pnsrc\">https://github.com/pnsrc</a>";
    const QString trustTunnelDetails = ru
            ? "Базовый VPN-движок: TrustTunnel<br/>"
              "Документация: <a href=\"https://trusttunnel.org/\">https://trusttunnel.org/</a><br/>"
              "Репозиторий: <a href=\"https://github.com/TrustTunnel/TrustTunnel\">https://github.com/TrustTunnel/TrustTunnel</a><br/>"
              "Кратко по docs:<ul>"
              "<li>Трафик маскируется под обычный HTTPS.</li>"
              "<li>Использует TLS и транспорт HTTP/2/HTTP/3.</li>"
              "<li>Поддерживает туннелирование TCP/UDP/ICMP.</li>"
              "<li>Есть split tunneling, system-wide tunnel и SOCKS5-режим.</li>"
              "</ul>"
            : "Core VPN engine: TrustTunnel<br/>"
              "Documentation: <a href=\"https://trusttunnel.org/\">https://trusttunnel.org/</a><br/>"
              "Repository: <a href=\"https://github.com/TrustTunnel/TrustTunnel\">https://github.com/TrustTunnel/TrustTunnel</a><br/>"
              "Docs summary:<ul>"
              "<li>Traffic is designed to look like regular HTTPS.</li>"
              "<li>Uses TLS with HTTP/2 and HTTP/3 transport.</li>"
              "<li>Supports TCP/UDP/ICMP tunneling.</li>"
              "<li>Includes split tunneling, system-wide tunnel, and SOCKS5 mode.</li>"
              "</ul>";
    const QString buildStamp = QString("%1 %2").arg(__DATE__).arg(__TIME__);
    auto *ftLabel = new QTextBrowser(aboutPage);
    ftLabel->setOpenExternalLinks(true);
    ftLabel->setMaximumHeight(92);
    ftLabel->setHtml(fireTunnelDetails);
    auto *ttLabel = new QTextBrowser(aboutPage);
    ttLabel->setOpenExternalLinks(true);
    ttLabel->setMaximumHeight(220);
    ttLabel->setHtml(trustTunnelDetails);
    aboutLayout->addRow(ru ? "Приложение:" : "Application:", new QLabel("FireTunnel (TrustTunnel Qt Client)", aboutPage));
    aboutLayout->addRow(ru ? "Версия:" : "Version:", new QLabel(QString::fromLatin1(FIRETUNNEL_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Git ревизия:" : "Git revision:", new QLabel(QString::fromLatin1(FIRETUNNEL_GIT_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Версия ядра TrustTunnel:" : "TrustTunnel core version:", new QLabel(QString::fromLatin1(TRUSTTUNNEL_VERSION), aboutPage));
    aboutLayout->addRow(ru ? "Сборка:" : "Build:", new QLabel(buildStamp, aboutPage));
    aboutLayout->addRow("Qt:", new QLabel(QString::fromLatin1(qVersion()), aboutPage));
    aboutLayout->addRow(ru ? "Хранилище конфигов:" : "Config storage:", new QLabel(storagePath(), aboutPage));
    aboutLayout->addRow(ru ? "О FireTunnel:" : "About FireTunnel:", ftLabel);
    aboutLayout->addRow(ru ? "О TrustTunnel:" : "About TrustTunnel:", ttLabel);
    addNavItem(ru ? "О программе" : "About", aboutPage, QIcon::fromTheme("help-about"));

    auto *licensePage = new QWidget(this);
    auto *licenseLayout = new QVBoxLayout(licensePage);
    auto *licenseView = new QTextBrowser(licensePage);
    licenseView->setReadOnly(true);
    licenseView->setFontFamily("Menlo, Consolas, monospace");
    licenseView->setOpenExternalLinks(true);
    licenseView->setLineWrapMode(QTextEdit::NoWrap);
    licenseView->setPlainText(loadLicenseText());
    licenseLayout->addWidget(licenseView);
    addNavItem(ru ? "Лицензии" : "Licenses", licensePage, QIcon::fromTheme("text-x-generic"));

    // Logging tab
    auto *logsPage = new QWidget(this);
    auto *logsLayout = new QFormLayout(logsPage);
    m_saveLogsCheck = new QCheckBox(ru ? "Сохранять логи в файл" : "Save logs to file", logsPage);
    m_saveLogsCheck->setChecked(settings.save_logs);
    m_logLevelCombo = new QComboBox(logsPage);
    m_logLevelCombo->addItems({"error", "warn", "info", "debug", "trace"});
    const int idx = m_logLevelCombo->findText(settings.log_level);
    m_logLevelCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    m_logPathEdit = new QLineEdit(settings.log_path, logsPage);
    auto *pickPathBtn = new QPushButton(ru ? "Выбрать..." : "Browse...", logsPage);
    auto *pathRow = new QHBoxLayout();
    pathRow->addWidget(m_logPathEdit);
    pathRow->addWidget(pickPathBtn);
    m_showLogsPanelCheck = new QCheckBox(ru ? "Показывать панель логов" : "Show logs panel", logsPage);
    m_showLogsPanelCheck->setChecked(settings.show_logs_panel);
    m_showTrafficCheck = new QCheckBox(ru ? "Показывать трафик в статусе" : "Show traffic in status bar", logsPage);
    m_showTrafficCheck->setChecked(settings.show_traffic_in_status);
    logsLayout->addRow(m_saveLogsCheck);
    logsLayout->addRow(ru ? "Уровень логов:" : "Log level:", m_logLevelCombo);
    logsLayout->addRow(ru ? "Файл логов:" : "Log file:", pathRow);
    logsLayout->addRow(m_showLogsPanelCheck);
    logsLayout->addRow(m_showTrafficCheck);
    addNavItem(ru ? "Логирование" : "Logging", logsPage, QIcon::fromTheme("document-open"));

    // Notifications tab
    auto *notifyPage = new QWidget(this);
    auto *notifyLayout = new QFormLayout(notifyPage);
    m_notifyOnStateCheck = new QCheckBox(ru ? "Уведомлять о смене состояния" : "Notify on state changes", notifyPage);
    m_notifyOnStateCheck->setChecked(settings.notify_on_state);
    m_notifyErrorsOnlyCheck = new QCheckBox(ru ? "Только при ошибках" : "Only on errors", notifyPage);
    m_notifyErrorsOnlyCheck->setChecked(settings.notify_only_errors);
    notifyLayout->addRow(m_notifyOnStateCheck);
    notifyLayout->addRow(m_notifyErrorsOnlyCheck);
    addNavItem(ru ? "Уведомления" : "Notifications", notifyPage, QIcon::fromTheme("preferences-desktop-notification"));

    auto *appearancePage = new QWidget(this);
    auto *appearanceLayout = new QFormLayout(appearancePage);
    m_themeModeCombo = new QComboBox(appearancePage);
    m_themeModeCombo->addItem("System", "system");
    m_themeModeCombo->addItem("Light", "light");
    m_themeModeCombo->addItem("Dark", "dark");
    int themeIdx = m_themeModeCombo->findData(settings.theme_mode);
    m_themeModeCombo->setCurrentIndex(themeIdx >= 0 ? themeIdx : 0);
    appearanceLayout->addRow(ru ? "Тема:" : "Theme:", m_themeModeCombo);
    m_autoConnectCheck = new QCheckBox(ru ? "Автоподключение при запуске" : "Auto-connect on app start", appearancePage);
    m_autoConnectCheck->setChecked(settings.auto_connect_on_start);
    appearanceLayout->addRow(QString(), m_autoConnectCheck);
    addNavItem(ru ? "Внешний вид" : "Appearance", appearancePage, QIcon::fromTheme("preferences-desktop-theme"));

    // Security tab
    auto *securityPage = new QWidget(this);
    auto *securityLayout = new QFormLayout(securityPage);
    m_killswitchCheck = new QCheckBox(ru ? "Kill switch (блокировать трафик вне VPN)" : "Kill switch (block untunneled traffic)", securityPage);
    m_killswitchCheck->setChecked(settings.killswitch_enabled);
    m_strictCertCheck = new QCheckBox(ru ? "Строгая проверка сертификата" : "Strict certificate verification", securityPage);
    m_strictCertCheck->setChecked(settings.strict_certificate_check);
    securityLayout->addRow(m_killswitchCheck);
    securityLayout->addRow(m_strictCertCheck);
    addNavItem(ru ? "Безопасность" : "Security", securityPage, QIcon::fromTheme("security-high"));

    auto *routingPage = new QWidget(this);
    auto *routingLayout = new QFormLayout(routingPage);
    m_routingEnableCheck = new QCheckBox(ru ? "Включить маршрутизацию" : "Enable routing", routingPage);
    m_routingEnableCheck->setChecked(settings.routing_enabled);
    m_routingTunnelRadio = new QRadioButton(ru ? "Туннелировать РФ" : "Tunnel RU", routingPage);
    m_routingBypassRadio = new QRadioButton(ru ? "Обходить РФ" : "Bypass RU", routingPage);
    if (settings.routing_mode == "bypass_ru") {
        m_routingBypassRadio->setChecked(true);
    } else {
        m_routingTunnelRadio->setChecked(true);
    }
    auto *modeRow = new QHBoxLayout();
    modeRow->addWidget(m_routingTunnelRadio);
    modeRow->addWidget(m_routingBypassRadio);
    m_routingUrlEdit = new QLineEdit(settings.routing_source_url, routingPage);
    m_routingCacheEdit = new QLineEdit(settings.routing_cache_path, routingPage);
    auto *browseCacheBtn = new QPushButton(ru ? "Обзор..." : "Browse...", routingPage);
    auto *cacheRow = new QHBoxLayout();
    cacheRow->addWidget(m_routingCacheEdit);
    cacheRow->addWidget(browseCacheBtn);
    routingLayout->addRow(m_routingEnableCheck);
    routingLayout->addRow(ru ? "Режим:" : "Mode:", modeRow);
    routingLayout->addRow(ru ? "URL списка подсетей:" : "Subnets URL:", m_routingUrlEdit);
    routingLayout->addRow(ru ? "Файл кеша:" : "Cache file:", cacheRow);
    addNavItem(ru ? "Маршрутизация" : "Routing", routingPage, QIcon::fromTheme("network-workgroup"));

    // ── Network tab: Custom DNS, Domain Bypass, Adapter Conflicts ──
    auto *networkPage = new QWidget(this);
    auto *networkLayout = new QVBoxLayout(networkPage);

    // --- Custom DNS ---
    auto *dnsCustomGroup = new QGroupBox(ru ? "Пользовательские DNS" : "Custom DNS Servers", networkPage);
    auto *dnsCustomLayout = new QVBoxLayout(dnsCustomGroup);
    m_customDnsCheck = new QCheckBox(ru ? "Использовать свои DNS-серверы" : "Use custom DNS servers", dnsCustomGroup);
    m_customDnsCheck->setChecked(settings.custom_dns_enabled);
    m_customDnsEdit = new QPlainTextEdit(dnsCustomGroup);
    m_customDnsEdit->setPlaceholderText(ru
            ? "Один DNS-сервер на строку:\n1.1.1.1\n8.8.8.8\ntls://1.1.1.1\nhttps://dns.adguard.com/dns-query"
            : "One DNS server per line:\n1.1.1.1\n8.8.8.8\ntls://1.1.1.1\nhttps://dns.adguard.com/dns-query");
    m_customDnsEdit->setMaximumHeight(100);
    m_customDnsEdit->setPlainText(settings.custom_dns_servers.join('\n'));
    m_customDnsEdit->setEnabled(settings.custom_dns_enabled);
    auto *dnsHint = new QLabel(ru
            ? "<i>Поддерживаемые форматы: 8.8.8.8:53, tcp://8.8.8.8:53, tls://1.1.1.1, "
              "https://dns.adguard.com/dns-query, quic://dns.adguard.com:8853, sdns://...</i>"
            : "<i>Supported: 8.8.8.8:53, tcp://8.8.8.8:53, tls://1.1.1.1, "
              "https://dns.adguard.com/dns-query, quic://dns.adguard.com:8853, sdns://...</i>",
            dnsCustomGroup);
    dnsHint->setWordWrap(true);
    dnsCustomLayout->addWidget(m_customDnsCheck);
    dnsCustomLayout->addWidget(m_customDnsEdit);
    dnsCustomLayout->addWidget(dnsHint);
    connect(m_customDnsCheck, &QCheckBox::toggled, m_customDnsEdit, &QPlainTextEdit::setEnabled);
    networkLayout->addWidget(dnsCustomGroup);

    // --- Domain bypass rules ---
    auto *bypassGroup = new QGroupBox(ru ? "Пропуск доменов (bypass)" : "Domain Bypass Rules", networkPage);
    auto *bypassLayout = new QVBoxLayout(bypassGroup);
    m_domainBypassCheck = new QCheckBox(ru
            ? "Включить правила обхода доменов"
            : "Enable domain bypass rules", bypassGroup);
    m_domainBypassCheck->setChecked(settings.domain_bypass_enabled);
    m_domainBypassEdit = new QPlainTextEdit(bypassGroup);
    m_domainBypassEdit->setPlaceholderText(ru
            ? "Один домен/маска на строку:\nexample.com\n*.google.com\n192.168.1.0/24\n10.0.0.0/8"
            : "One domain/mask per line:\nexample.com\n*.google.com\n192.168.1.0/24\n10.0.0.0/8");
    m_domainBypassEdit->setMaximumHeight(120);
    m_domainBypassEdit->setPlainText(settings.domain_bypass_rules.join('\n'));
    m_domainBypassEdit->setEnabled(settings.domain_bypass_enabled);
    auto *bypassHint = new QLabel(ru
            ? "<i>Домены и адреса, перечисленные здесь, будут добавлены в исключения (exclusions) конфига. "
              "Форматы: domain.com, *.domain.com, IP, IP:port, CIDR (IP/mask).</i>"
            : "<i>Domains and addresses listed here are added to config exclusions. "
              "Formats: domain.com, *.domain.com, IP, IP:port, CIDR (IP/mask).</i>",
            bypassGroup);
    bypassHint->setWordWrap(true);
    bypassLayout->addWidget(m_domainBypassCheck);
    bypassLayout->addWidget(m_domainBypassEdit);
    bypassLayout->addWidget(bypassHint);
    connect(m_domainBypassCheck, &QCheckBox::toggled, m_domainBypassEdit, &QPlainTextEdit::setEnabled);
    networkLayout->addWidget(bypassGroup);

    // --- Adapter conflict scanner ---
    auto *conflictGroup = new QGroupBox(ru ? "Конфликты адаптеров" : "Adapter Conflicts", networkPage);
    auto *conflictLayout = new QVBoxLayout(conflictGroup);
    m_scanConflictsCheck = new QCheckBox(ru
            ? "Сканировать конфликты при подключении"
            : "Scan for adapter conflicts on connect", conflictGroup);
    m_scanConflictsCheck->setChecked(settings.scan_adapter_conflicts);
    auto *conflictHint = new QLabel(ru
            ? "<i>Перед подключением приложение проверит наличие конфликтующих VPN-адаптеров "
              "(Radmin VPN, Hamachi, OpenVPN TAP и др.) и предупредит, если они могут мешать.</i>"
            : "<i>Before connecting, the app checks for conflicting VPN adapters "
              "(Radmin VPN, Hamachi, OpenVPN TAP, etc.) and warns if they may interfere.</i>",
            conflictGroup);
    conflictHint->setWordWrap(true);
    auto *scanNowBtn = new QPushButton(ru ? "Сканировать сейчас" : "Scan Now", conflictGroup);
    conflictLayout->addWidget(m_scanConflictsCheck);
    conflictLayout->addWidget(conflictHint);
    conflictLayout->addWidget(scanNowBtn);
    networkLayout->addWidget(conflictGroup);

    connect(scanNowBtn, &QPushButton::clicked, this, [this]() {
        emit advancedAction("scan_conflicts");
    });

    networkLayout->addStretch();
    addNavItem(ru ? "Сеть" : "Network", networkPage, QIcon::fromTheme("network-wired"));

    // ── Advanced settings tab ─────────────────────────────
    auto *advancedPage = new QWidget(this);
    auto *advancedLayout = new QVBoxLayout(advancedPage);

    auto *tunnelGroup = new QGroupBox(ru ? "Сетевые адаптеры" : "Network Adapters", advancedPage);
    auto *tunnelGroupLayout = new QVBoxLayout(tunnelGroup);
    auto *reinstallBtn = new QPushButton(ru ? "Переустановить туннели" : "Reinstall Tunnel Adapters", tunnelGroup);
    reinstallBtn->setToolTip(ru
            ? "Удаляет все виртуальные сетевые адаптеры (WinTUN) и пересоздаёт их при следующем подключении."
            : "Removes all virtual network adapters (WinTUN) and recreates them on next connection.");
    auto *reinstallNote = new QLabel(
            ru ? "<i>Если VPN не подключается — попробуйте переустановить адаптеры.</i>"
               : "<i>If VPN fails to connect, try reinstalling the adapters.</i>",
            tunnelGroup);
    reinstallNote->setWordWrap(true);
    tunnelGroupLayout->addWidget(reinstallBtn);
    tunnelGroupLayout->addWidget(reinstallNote);
    advancedLayout->addWidget(tunnelGroup);

    // ── Adapter discovery & deactivation (Windows) ──
    auto *adapterGroup = new QGroupBox(ru ? "Обнаруженные VPN-адаптеры" : "Detected VPN Adapters", advancedPage);
    auto *adapterGroupLayout = new QVBoxLayout(adapterGroup);
    auto *adapterHint = new QLabel(
            ru ? "<i>Сторонние VPN-адаптеры (Radmin VPN, Hamachi, WireGuard и др.) могут конфликтовать "
                 "с адаптером от FireTunnel. Рекомендуется деактивировать их перед подключением.</i>"
               : "<i>Third-party VPN adapters (Radmin VPN, Hamachi, WireGuard, etc.) may conflict "
                 "with the FireTunnel adapter. It is recommended to deactivate them before connecting.</i>",
            adapterGroup);
    adapterHint->setWordWrap(true);
    adapterGroupLayout->addWidget(adapterHint);

    m_adapterTree = new QTreeWidget(adapterGroup);
    m_adapterTree->setHeaderLabels({
        ru ? "Адаптер" : "Adapter",
        ru ? "Описание" : "Description",
        ru ? "Статус" : "Status",
        ""  // action column
    });
    m_adapterTree->setRootIsDecorated(false);
    m_adapterTree->setAlternatingRowColors(true);
    m_adapterTree->setMinimumHeight(140);
    m_adapterTree->header()->setStretchLastSection(false);
    m_adapterTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_adapterTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_adapterTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_adapterTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    adapterGroupLayout->addWidget(m_adapterTree);

    auto *rescanAdaptersBtn = new QPushButton(ru ? "Обновить список" : "Refresh List", adapterGroup);
    adapterGroupLayout->addWidget(rescanAdaptersBtn);
    advancedLayout->addWidget(adapterGroup);

    // Lambda to populate the adapter tree
    auto populateAdapters = [this, ru]() {
        m_adapterTree->clear();
        const auto adapters = m_adapterManager.scanAdapters();
        for (const auto &info : adapters) {
            auto *item = new QTreeWidgetItem(m_adapterTree);
            item->setText(0, info.name);
            item->setText(1, info.description);
            item->setText(2, info.enabled ? (ru ? "Активен" : "Enabled")
                                          : (ru ? "Отключён" : "Disabled"));
            if (info.isOurs) {
                // Our adapter — just show info, no deactivation button
                item->setForeground(0, QColor("#22aa22"));
                item->setText(3, "FireTunnel");
            } else {
                // Third-party adapter — add Deactivate/Activate button
                auto *btn = new QPushButton(
                        info.enabled ? (ru ? "Деактивировать" : "Deactivate")
                                     : (ru ? "Активировать" : "Activate"),
                        m_adapterTree);
                if (info.enabled) {
                    btn->setStyleSheet("QPushButton { color: #cc3333; }");
                } else {
                    btn->setStyleSheet("QPushButton { color: #22aa22; }");
                }
                const QString adapterName = info.name;
                const bool wasEnabled = info.enabled;
                connect(btn, &QPushButton::clicked, this, [this, adapterName, wasEnabled, ru]() {
                    bool ok = false;
                    if (wasEnabled) {
                        auto res = QMessageBox::question(this,
                            ru ? "Деактивация адаптера" : "Deactivate Adapter",
                            QString(ru ? "Деактивировать адаптер '%1'?\n\n"
                                  "Данный адаптер может конфликтовать с адаптером от FireTunnel, "
                                  "по этому его рекомендуется деактивировать."
                                : "Deactivate adapter '%1'?\n\n"
                                  "This adapter may conflict with the FireTunnel adapter "
                                  "and it is recommended to deactivate it.")
                                .arg(adapterName),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (res == QMessageBox::Yes) {
                            ok = m_adapterManager.disableAdapter(adapterName);
                        }
                    } else {
                        ok = m_adapterManager.enableAdapter(adapterName);
                    }
                    if (ok) {
                        emit advancedAction("refresh_adapters");
                        // Re-populate the tree in-place
                        QTimer::singleShot(500, this, [this]() {
                            emit advancedAction("rescan_adapters_ui");
                        });
                    }
                });
                m_adapterTree->setItemWidget(item, 3, btn);
                item->setForeground(0, QColor("#cc3333"));
            }
        }
        if (adapters.isEmpty()) {
            auto *emptyItem = new QTreeWidgetItem(m_adapterTree);
            emptyItem->setText(0, ru ? "Адаптеры не обнаружены" : "No adapters found");
            emptyItem->setFlags(emptyItem->flags() & ~Qt::ItemIsSelectable);
        }
    };

    // Initial population
    populateAdapters();

    connect(rescanAdaptersBtn, &QPushButton::clicked, this, populateAdapters);
    // Handle re-scan after deactivation
    connect(this, &SettingsDialog::advancedAction, this, [populateAdapters](const QString &action) {
        if (action == "rescan_adapters_ui") {
            populateAdapters();
        }
    });

    // ── SSH / P2P Bypass ──
    auto *bypassTrafficGroup = new QGroupBox(ru ? "Обход трафика" : "Traffic Bypass", advancedPage);
    auto *bypassTrafficLayout = new QVBoxLayout(bypassTrafficGroup);
    auto *bypassTrafficHint = new QLabel(
            ru ? "<i>Выберите типы трафика, которые должны идти мимо VPN-туннеля (напрямую).</i>"
               : "<i>Select traffic types that should bypass the VPN tunnel (go direct).</i>",
            bypassTrafficGroup);
    bypassTrafficHint->setWordWrap(true);
    bypassTrafficLayout->addWidget(bypassTrafficHint);

    m_sshBypassCheck = new QCheckBox(ru ? "Обходить SSH-трафик (порт 22)" : "Bypass SSH traffic (port 22)", bypassTrafficGroup);
    m_sshBypassCheck->setChecked(settings.ssh_bypass_enabled);
    m_sshBypassCheck->setToolTip(ru
            ? "SSH-соединения (порт 22) будут идти напрямую, минуя VPN-туннель."
            : "SSH connections (port 22) will go direct, bypassing the VPN tunnel.");
    bypassTrafficLayout->addWidget(m_sshBypassCheck);

    m_p2pBypassCheck = new QCheckBox(ru ? "Обходить P2P-трафик (BitTorrent и др.)" : "Bypass P2P traffic (BitTorrent, etc.)", bypassTrafficGroup);
    m_p2pBypassCheck->setChecked(settings.p2p_bypass_enabled);
    m_p2pBypassCheck->setToolTip(ru
            ? "P2P-трафик (BitTorrent, DHT и др.) будет идти напрямую, минуя VPN-туннель."
            : "P2P traffic (BitTorrent, DHT, etc.) will go direct, bypassing the VPN tunnel.");
    bypassTrafficLayout->addWidget(m_p2pBypassCheck);

    advancedLayout->addWidget(bypassTrafficGroup);

    auto *dnsGroup = new QGroupBox("DNS", advancedPage);
    auto *dnsGroupLayout = new QVBoxLayout(dnsGroup);
    auto *flushDnsBtn = new QPushButton(ru ? "Сбросить DNS-кеш" : "Flush DNS Cache", dnsGroup);
    flushDnsBtn->setToolTip(ru
            ? "Запускает очистку системного DNS-кеша (ipconfig /flushdns на Windows, dscacheutil -flushcache на macOS)."
            : "Runs system DNS cache flush (ipconfig /flushdns on Windows, dscacheutil -flushcache on macOS).");
    dnsGroupLayout->addWidget(flushDnsBtn);
    advancedLayout->addWidget(dnsGroup);

    auto *cacheGroup = new QGroupBox(ru ? "Кеш и данные" : "Cache & Data", advancedPage);
    auto *cacheGroupLayout = new QVBoxLayout(cacheGroup);
    auto *clearSslBtn = new QPushButton(ru ? "Очистить кеш SSL-сессий" : "Clear SSL Session Cache", cacheGroup);
    clearSslBtn->setToolTip(ru
            ? "Удаляет сохранённые TLS-сессии. Может помочь при проблемах с подключением."
            : "Deletes saved TLS sessions. May help with connection issues.");
    auto *resetSettingsBtn = new QPushButton(ru ? "Сбросить все настройки" : "Reset All Settings", cacheGroup);
    resetSettingsBtn->setStyleSheet("QPushButton { color: #cc3333; }");
    resetSettingsBtn->setToolTip(ru
            ? "Возвращает все настройки приложения к значениям по умолчанию."
            : "Resets all application settings to their defaults.");
    cacheGroupLayout->addWidget(clearSslBtn);
    cacheGroupLayout->addWidget(resetSettingsBtn);
    advancedLayout->addWidget(cacheGroup);

    advancedLayout->addStretch();
    addNavItem(ru ? "Расширенные" : "Advanced", advancedPage, QIcon::fromTheme("preferences-other"));

    connect(reinstallBtn, &QPushButton::clicked, this, [this, ru]() {
        auto res = QMessageBox::question(this,
                ru ? "Переустановка туннелей" : "Reinstall Tunnels",
                ru ? "Виртуальные адаптеры будут удалены и пересозданы при следующем подключении. Продолжить?"
                   : "Virtual adapters will be removed and recreated on next connection. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res == QMessageBox::Yes) {
            m_reinstallTunnels = true;
            emit advancedAction("reinstall_tunnels");
        }
    });

    connect(flushDnsBtn, &QPushButton::clicked, this, [this]() {
        m_flushDns = true;
        emit advancedAction("flush_dns");
    });

    connect(clearSslBtn, &QPushButton::clicked, this, [this, ru]() {
        m_clearSslCache = true;
        QMessageBox::information(this,
                ru ? "Кеш SSL" : "SSL Cache",
                ru ? "Кеш SSL-сессий будет очищен." : "SSL session cache will be cleared.");
        emit advancedAction("clear_ssl_cache");
    });

    connect(resetSettingsBtn, &QPushButton::clicked, this, [this, ru]() {
        auto res = QMessageBox::warning(this,
                ru ? "Сброс настроек" : "Reset Settings",
                ru ? "Все настройки приложения будут сброшены к значениям по умолчанию. Это действие нельзя отменить. Продолжить?"
                   : "All application settings will be reset to defaults. This cannot be undone. Continue?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (res == QMessageBox::Yes) {
            m_resetSettings = true;
            accept(); // close dialog, MainWindow will handle reset
        }
    });

    connect(browseCacheBtn, &QPushButton::clicked, this, [this, ru]() {
        const QString path = QFileDialog::getSaveFileName(
                this,
                ru ? "Путь кеша подсетей" : "Cache file path",
                m_routingCacheEdit->text(),
                ru ? "Text files (*.lst *.txt);;All files (*)" : "Text files (*.lst *.txt);;All files (*)");
        if (!path.isEmpty()) {
            m_routingCacheEdit->setText(path);
        }
    });

    connect(pickPathBtn, &QPushButton::clicked, this, [this, ru]() {
        const QString path = QFileDialog::getSaveFileName(
                this,
                ru ? "Путь файла логов" : "Log file path",
                m_logPathEdit->text().isEmpty() ? "/tmp/trusttunnel-qt.log" : m_logPathEdit->text(),
                ru ? "Log files (*.log);;All files (*)" : "Log files (*.log);;All files (*)");
        if (!path.isEmpty()) {
            m_logPathEdit->setText(path);
        }
    });

    bodyLayout->addWidget(m_navList);
    bodyLayout->addWidget(m_stack, 1);
    layout->addLayout(bodyLayout);

    connect(m_navList, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    if (m_navList->count() > 0) {
        m_navList->setCurrentRow(0);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

bool SettingsDialog::saveLogs() const { return m_saveLogsCheck && m_saveLogsCheck->isChecked(); }
QString SettingsDialog::logLevel() const { return m_logLevelCombo ? m_logLevelCombo->currentText() : "info"; }
QString SettingsDialog::logPath() const { return m_logPathEdit ? m_logPathEdit->text().trimmed() : QString(); }
QString SettingsDialog::themeMode() const { return m_themeModeCombo ? m_themeModeCombo->currentData().toString() : "system"; }
bool SettingsDialog::autoConnectOnStart() const { return m_autoConnectCheck && m_autoConnectCheck->isChecked(); }
bool SettingsDialog::showLogsPanel() const { return m_showLogsPanelCheck && m_showLogsPanelCheck->isChecked(); }
bool SettingsDialog::showTrafficInStatus() const { return m_showTrafficCheck && m_showTrafficCheck->isChecked(); }
bool SettingsDialog::notifyOnState() const { return m_notifyOnStateCheck && m_notifyOnStateCheck->isChecked(); }
bool SettingsDialog::notifyOnlyErrors() const { return m_notifyErrorsOnlyCheck && m_notifyErrorsOnlyCheck->isChecked(); }
bool SettingsDialog::killswitchEnabled() const { return m_killswitchCheck && m_killswitchCheck->isChecked(); }
bool SettingsDialog::strictCertificateCheck() const { return m_strictCertCheck && m_strictCertCheck->isChecked(); }
bool SettingsDialog::routingEnabled() const { return m_routingEnableCheck && m_routingEnableCheck->isChecked(); }
QString SettingsDialog::routingMode() const {
    if (m_routingBypassRadio && m_routingBypassRadio->isChecked()) return "bypass_ru";
    return "tunnel_ru";
}
QString SettingsDialog::routingSourceUrl() const { return m_routingUrlEdit ? m_routingUrlEdit->text().trimmed() : QString(); }
QString SettingsDialog::routingCachePath() const { return m_routingCacheEdit ? m_routingCacheEdit->text().trimmed() : QString(); }
bool SettingsDialog::reinstallTunnelsRequested() const { return m_reinstallTunnels; }
bool SettingsDialog::flushDnsRequested() const { return m_flushDns; }
bool SettingsDialog::clearSslCacheRequested() const { return m_clearSslCache; }
bool SettingsDialog::resetSettingsRequested() const { return m_resetSettings; }
bool SettingsDialog::customDnsEnabled() const { return m_customDnsCheck && m_customDnsCheck->isChecked(); }
QStringList SettingsDialog::customDnsServers() const {
    if (!m_customDnsEdit) return {};
    const QString text = m_customDnsEdit->toPlainText().trimmed();
    if (text.isEmpty()) return {};
    QStringList lines;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
            lines.append(trimmed);
        }
    }
    return lines;
}
bool SettingsDialog::domainBypassEnabled() const { return m_domainBypassCheck && m_domainBypassCheck->isChecked(); }
QStringList SettingsDialog::domainBypassRules() const {
    if (!m_domainBypassEdit) return {};
    const QString text = m_domainBypassEdit->toPlainText().trimmed();
    if (text.isEmpty()) return {};
    QStringList lines;
    for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty() && !trimmed.startsWith('#')) {
            lines.append(trimmed);
        }
    }
    return lines;
}
bool SettingsDialog::scanAdapterConflicts() const { return m_scanConflictsCheck && m_scanConflictsCheck->isChecked(); }
bool SettingsDialog::sshBypassEnabled() const { return m_sshBypassCheck && m_sshBypassCheck->isChecked(); }
bool SettingsDialog::p2pBypassEnabled() const { return m_p2pBypassCheck && m_p2pBypassCheck->isChecked(); }

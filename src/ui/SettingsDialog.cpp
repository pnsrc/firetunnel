#include "SettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "ConfigInspector.h"
#include "ConfigStore.h"
#include "vpn/trusttunnel/version.h"

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
    auto *tabs = new QTabWidget(this);

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
    tabs->addTab(aboutPage, ru ? "О программе" : "About");

    auto *licensePage = new QWidget(this);
    auto *licenseLayout = new QVBoxLayout(licensePage);
    auto *licenseView = new QTextBrowser(licensePage);
    licenseView->setReadOnly(true);
    licenseView->setFontFamily("Menlo, Consolas, monospace");
    licenseView->setOpenExternalLinks(true);
    licenseView->setLineWrapMode(QTextEdit::NoWrap);
    licenseView->setPlainText(loadLicenseText());
    licenseLayout->addWidget(licenseView);
    tabs->addTab(licensePage, ru ? "Лицензии" : "Licenses");

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
    logsLayout->addRow(m_saveLogsCheck);
    logsLayout->addRow(ru ? "Уровень логов:" : "Log level:", m_logLevelCombo);
    logsLayout->addRow(ru ? "Файл логов:" : "Log file:", pathRow);
    tabs->addTab(logsPage, ru ? "Логирование" : "Logging");

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
    tabs->addTab(appearancePage, ru ? "Внешний вид" : "Appearance");

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
    tabs->addTab(routingPage, ru ? "Маршрутизация" : "Routing");

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

    layout->addWidget(tabs);

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
bool SettingsDialog::routingEnabled() const { return m_routingEnableCheck && m_routingEnableCheck->isChecked(); }
QString SettingsDialog::routingMode() const {
    if (m_routingBypassRadio && m_routingBypassRadio->isChecked()) return "bypass_ru";
    return "tunnel_ru";
}
QString SettingsDialog::routingSourceUrl() const { return m_routingUrlEdit ? m_routingUrlEdit->text().trimmed() : QString(); }
QString SettingsDialog::routingCachePath() const { return m_routingCacheEdit ? m_routingCacheEdit->text().trimmed() : QString(); }

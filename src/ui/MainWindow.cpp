#include "MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QInputDialog>
#include <QPalette>
#include <QProgressDialog>
#include <QPushButton>
#include <QPointer>
#include <QToolButton>
#include <QStatusBar>
#include <QStyle>
#include <QStyleHints>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>
#include <QToolButton>
#include <QtGlobal>

#include "common/logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#include "AppSettings.h"
#include "AppUiUtils.h"
#include "ConfigInspector.h"
#include "ConfigStore.h"
#include "SettingsDialog.h"
#include "qt_trusttunnel_client.h"

static ag::LogLevel parseLogLevel(const QString &level) {
    const QString l = level.toLower();
    if (l == "error") return ag::LOG_LEVEL_ERROR;
    if (l == "warn" || l == "warning") return ag::LOG_LEVEL_WARN;
    if (l == "debug") return ag::LOG_LEVEL_DEBUG;
    if (l == "trace") return ag::LOG_LEVEL_TRACE;
    return ag::LOG_LEVEL_INFO;
}

class MainWindow : public QMainWindow {
public:
    MainWindow() {
#ifndef _WIN32
        m_isRoot = (::geteuid() == 0);
#endif
        m_appSettings = loadAppSettings();

        enforceFirstRunElevation();

        setupUi();
        setupLogic();
        applyTheme();
        applyLanguage(m_appSettings.language == "ru" ? "ru" : "en");
        refreshStoredList();
        processStartupArguments();
        if (m_configPath->text().trimmed().isEmpty() && m_configsList->count() > 0) {
            m_configsList->setCurrentRow(0);
            if (m_configsList->currentItem()) {
                m_configPath->setText(m_configsList->currentItem()->text());
            }
        }
        if (m_appSettings.auto_connect_on_start) {
            QTimer::singleShot(400, this, [this]() {
                if (!m_configPath->text().trimmed().isEmpty()) {
                    m_connectButton->click();
                } else {
                    log(tr("Auto-connect skipped: no config selected"));
                }
            });
        }

        // Restore logs panel visibility from settings
        if (m_toggleLogsAction) {
            m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
            m_logBox->setVisible(m_appSettings.show_logs_panel);
        }

        // Restore notification toggles in tray availability
        if (!m_appSettings.notify_on_state && m_tray) {
            m_tray->hide();
            m_tray->show(); // ensure icon exists but notifications obey setting
        }
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (!m_forceExit && m_tray && m_tray->isVisible()) {
            hide();
            statusBar()->showMessage(tr("Running in tray"), 3000);
            m_tray->showMessage(windowTitle(), tr("Application minimized to tray"), QSystemTrayIcon::Information, 1500);
            event->ignore();
            return;
        }
        QMainWindow::closeEvent(event);
    }

    bool eventFilter(QObject *obj, QEvent *event) override {
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        if (obj == qApp && event->type() == QEvent::ApplicationPaletteChange && m_appSettings.theme_mode == "system") {
            applyTheme();
        }
#endif
        return QMainWindow::eventFilter(obj, event);
    }

private:
    bool selectConfigPath(const QString &path) {
        const QString normalized = QFileInfo(path).absoluteFilePath();
        if (!QFileInfo::exists(normalized)) {
            return false;
        }
        m_configPath->setText(normalized);
        addCurrentToStorage();
        refreshStoredList();
        return true;
    }

    bool importConfigFromDeeplink(const QString &deeplink) {
        const QUrl url = QUrl::fromUserInput(deeplink.trimmed());
        if (!url.isValid()) {
            log(tr("Invalid deeplink"));
            return false;
        }
        const QString scheme = url.scheme().toLower();
        if (scheme != "trusttunnel" && scheme != "firetunnel") {
            log(tr("Unsupported deeplink scheme: %1").arg(url.scheme()));
            return false;
        }

        const QUrlQuery query(url);
        QString path = query.queryItemValue("path");
        if (path.isEmpty()) {
            path = query.queryItemValue("config");
        }
        if (!path.isEmpty()) {
            if (selectConfigPath(path)) {
                statusBar()->showMessage(tr("Config added from deeplink"), 2500);
                return true;
            }
            log(tr("Deeplink config path not found: %1").arg(path));
            return false;
        }

        const QString base64Toml = query.queryItemValue("b64");
        if (!base64Toml.isEmpty()) {
            const QByteArray decoded = QByteArray::fromBase64(base64Toml.toUtf8());
            if (decoded.isEmpty()) {
                log(tr("Deeplink b64 payload is empty or invalid"));
                return false;
            }
            const QString defaultName = QString("imported-%1.toml").arg(QDateTime::currentSecsSinceEpoch());
            const QString name = query.queryItemValue("name").trimmed().isEmpty()
                    ? defaultName
                    : query.queryItemValue("name").trimmed();
            const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
            QDir().mkpath(base);
            const QString target = QDir(base).filePath(name.endsWith(".toml") ? name : (name + ".toml"));
            QFile f(target);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                log(tr("Failed to write imported config: %1").arg(target));
                return false;
            }
            f.write(decoded);
            f.close();
            selectConfigPath(target);
            statusBar()->showMessage(tr("Config imported from deeplink payload"), 2500);
            return true;
        }

        log(tr("Deeplink must contain path/config or b64 payload"));
        return false;
    }

    bool downloadRoutingList(const QString &url, const QString &targetPath) {
        QNetworkAccessManager mgr;
        QNetworkRequest req(url);
        QEventLoop loop;
        QNetworkReply *reply = mgr.get(req);
        QTimer timeout;
        timeout.setSingleShot(true);
        timeout.start(8000);
        connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (timeout.isActive()) {
            timeout.stop();
        } else {
            reply->abort();
            log(tr("Routing download timed out"));
            reply->deleteLater();
            return false;
        }

        if (reply->error() != QNetworkReply::NoError) {
            log(tr("Routing download failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return false;
        }
        QByteArray data = reply->readAll();
        reply->deleteLater();
        QFileInfo info(targetPath);
        QDir().mkpath(info.absolutePath());
        QFile f(targetPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            log(tr("Cannot write routing cache: %1").arg(targetPath));
            return false;
        }
        f.write(data);
        return true;
    }

    bool prepareRoutingRules(std::vector<std::string> &includeOut, std::vector<std::string> &excludeOut) {
        if (!m_appSettings.routing_enabled) {
            return true;
        }
        const QString cache = m_appSettings.routing_cache_path;
        const QString url = m_appSettings.routing_source_url;
        QFileInfo fi(cache);
        std::unique_ptr<QProgressDialog> routingProgress;
        if (!fi.exists() || fi.size() == 0) {
            log(tr("Routing cache missing, downloading..."));
            routingProgress = std::make_unique<QProgressDialog>(
                    tr("Downloading routing list..."), QString(), 0, 0, this,
                    Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint);
            routingProgress->setWindowModality(Qt::ApplicationModal);
            routingProgress->setAutoClose(true);
            routingProgress->setMinimumDuration(0);
            routingProgress->setRange(0, 0);
            routingProgress->show();
            qApp->processEvents(QEventLoop::AllEvents, 50);
            if (!downloadRoutingList(url, cache)) {
                QMessageBox::warning(this, tr("Routing"),
                        tr("Failed to download routing list.\nCheck connection and try again."));
                if (routingProgress) routingProgress->close();
                return false;
            }
            log(tr("Routing list cached to %1").arg(cache));
            if (routingProgress) routingProgress->close();
        }
        QFile f(cache);
        if (!f.open(QIODevice::ReadOnly)) {
            log(tr("Failed to open routing cache: %1").arg(cache));
            return false;
        }
        includeOut.clear();
        excludeOut.clear();
        while (!f.atEnd()) {
            const QByteArray line = f.readLine();
            QString s = QString::fromUtf8(line).trimmed();
            if (s.isEmpty() || s.startsWith('#')) continue;
            if (m_appSettings.routing_mode == "bypass_ru") {
                excludeOut.emplace_back(s.toStdString());
            } else {
                includeOut.emplace_back(s.toStdString());
            }
        }
        const int count = static_cast<int>(includeOut.size() + excludeOut.size());
        log(tr("Routing rules loaded: %1 entries").arg(count));
        return true;
    }

    bool relaunchElevated() {
#ifndef _WIN32
        QString errorText;
        const QString exe = QCoreApplication::applicationFilePath();
        const QString cmd = QString("exec %1 >/tmp/trusttunnel-qt.relaunch.log 2>&1 &").arg(shellEscape(exe));
        if (!runElevatedShell(cmd, &errorText)) {
            QMessageBox::critical(this, tr("Elevation Failed"),
                    tr("Failed to restart with administrator privileges.\n\n%1").arg(errorText));
            return false;
        }
        m_forceExit = true;
        qApp->quit();
        return true;
#else
        const QString exe = QCoreApplication::applicationFilePath();
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.hwnd = reinterpret_cast<HWND>(this->winId());
        sei.lpVerb = L"runas";
        std::wstring wexe = exe.toStdWString();
        sei.lpFile = wexe.c_str();
        sei.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&sei)) {
            QMessageBox::critical(this, tr("Elevation Failed"),
                    tr("Failed to restart with administrator privileges.\n\nError code: %1")
                            .arg(QString::number(GetLastError())));
            return false;
        }
        m_forceExit = true;
        qApp->quit();
        return true;
#endif
    }

#ifdef _WIN32
    bool isProcessElevated() const {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }
        TOKEN_ELEVATION elevation{};
        DWORD size = 0;
        const BOOL ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
        CloseHandle(token);
        return ok && elevation.TokenIsElevated;
    }
#endif

    void processStartupArguments() {
        const QStringList args = QCoreApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            const QString arg = args.at(i).trimmed();
            if (arg.isEmpty()) {
                continue;
            }
            if (arg.startsWith("trusttunnel://", Qt::CaseInsensitive)
                    || arg.startsWith("firetunnel://", Qt::CaseInsensitive)) {
                importConfigFromDeeplink(arg);
                continue;
            }
            if (arg.endsWith(".toml", Qt::CaseInsensitive)) {
                selectConfigPath(arg);
            }
        }
    }

    void createConfigFromTemplate() {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Create Config"));
        auto *layout = new QVBoxLayout(&dlg);
        auto *form = new QFormLayout();
        auto *hostEdit = new QLineEdit("server.example.com", &dlg);
        auto *addrEdit = new QLineEdit("server.example.com:443", &dlg);
        auto *userEdit = new QLineEdit("user", &dlg);
        auto *passEdit = new QLineEdit("password", &dlg);
        passEdit->setEchoMode(QLineEdit::Password);
        form->addRow(tr("Host:"), hostEdit);
        form->addRow(tr("Address host:port:"), addrEdit);
        form->addRow(tr("Username:"), userEdit);
        form->addRow(tr("Password:"), passEdit);
        layout->addLayout(form);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);
        if (dlg.exec() != QDialog::Accepted) {
            return;
        }

        const QString targetPath = QFileDialog::getSaveFileName(
                this,
                tr("Save TrustTunnel config"),
                QDir::homePath() + "/trusttunnel-config.toml",
                tr("TOML files (*.toml);;All files (*)"));
        if (targetPath.isEmpty()) {
            return;
        }

        const QString toml = QString(
                                     "loglevel = \"info\"\n"
                                     "vpn_mode = \"tunnel\"\n"
                                     "killswitch_enabled = true\n\n"
                                     "[endpoint]\n"
                                     "hostname = \"%1\"\n"
                                     "addresses = [\"%2\"]\n"
                                     "upstream_protocol = \"http2\"\n"
                                     "upstream_fallback_protocol = \"http\"\n"
                                     "username = \"%3\"\n"
                                     "password = \"%4\"\n\n"
                                     "[listener.tun]\n"
                                     "name = \"\"\n"
                                     "mtu_size = 1500\n"
                                     "change_system_dns = true\n")
                                     .arg(hostEdit->text().trimmed(),
                                             addrEdit->text().trimmed(),
                                             userEdit->text().trimmed(),
                                             passEdit->text());
        QFile f(targetPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Config"), tr("Failed to save config file."));
            return;
        }
        f.write(toml.toUtf8());
        f.close();
        selectConfigPath(targetPath);
        statusBar()->showMessage(tr("Config created"), 2500);
    }

    void setupUi() {
        setWindowTitle("FireTunnel");
        resize(980, 650);
        setWindowIcon(makeAppIcon());
        m_appMenu = menuBar()->addMenu("App");
        auto *appMenu = m_appMenu;
        m_settingsAction = appMenu->addAction("Settings");
        m_importDeeplinkAction = appMenu->addAction("Import Deeplink");
        m_createConfigAction = appMenu->addAction("Create Config");
        appMenu->addSeparator();
        m_quitAction = appMenu->addAction("Quit");

        m_viewMenu = menuBar()->addMenu("View");
        auto *viewMenu = m_viewMenu;
        m_toggleLogsAction = viewMenu->addAction("Hide Logs");
        m_toggleLogsAction->setCheckable(true);
        m_toggleLogsAction->setChecked(true);

        m_languageMenu = menuBar()->addMenu("Language");
        auto *languageMenu = m_languageMenu;
        auto *langGroup = new QActionGroup(this);
        langGroup->setExclusive(true);
        m_langEnAction = languageMenu->addAction("English");
        m_langEnAction->setCheckable(true);
        m_langRuAction = languageMenu->addAction("Русский");
        m_langRuAction->setCheckable(true);
        langGroup->addAction(m_langEnAction);
        langGroup->addAction(m_langRuAction);
        m_langEnAction->setChecked(true);

        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);

        auto *topRow = new QHBoxLayout();
        topRow->setSpacing(12);

        m_configsBox = new QGroupBox(root);
        m_configsBox->setObjectName("configsBox");
        auto *configsLayout = new QVBoxLayout(m_configsBox);
        m_configsList = new QListWidget(m_configsBox);
        auto *configsButtons = new QHBoxLayout();
        m_addConfigButton = new QPushButton(m_configsBox);
        m_removeConfigButton = new QPushButton(m_configsBox);
        m_pingConfigButton = new QPushButton(m_configsBox);
        configsButtons->addWidget(m_addConfigButton);
        configsButtons->addWidget(m_removeConfigButton);
        configsButtons->addWidget(m_pingConfigButton);
        configsLayout->addWidget(m_configsList);
        configsLayout->addLayout(configsButtons);

        m_controlBox = new QGroupBox(root);
        m_controlBox->setObjectName("controlBox");
        auto *controlLayout = new QVBoxLayout(m_controlBox);
        controlLayout->setSpacing(10);
        auto *pathRow = new QHBoxLayout();
        m_configPath = new QLineEdit(m_controlBox);
        m_browseButton = new QPushButton(m_controlBox);
        m_viewConfigButton = new QPushButton(m_controlBox);
        m_connectButton = new QPushButton(m_controlBox);
        m_disconnectButton = new QPushButton(m_controlBox);
        m_stateLabel = new QLabel(m_controlBox);
        m_stateLabel->setObjectName("stateLabel");
        m_stateLabel->setAlignment(Qt::AlignCenter);
        m_stateLabel->setText(tr("VPN: Disconnected"));
        m_disconnectButton->setEnabled(false);
        m_connectButton->setObjectName("connectButton");
        m_disconnectButton->setObjectName("disconnectButton");
        m_connectButton->setMinimumHeight(42);
        m_disconnectButton->setMinimumHeight(42);

        m_configPath->setPlaceholderText("Path to TrustTunnel TOML config");
        pathRow->addWidget(m_configPath);
        pathRow->addWidget(m_browseButton);
        pathRow->addWidget(m_viewConfigButton);
        auto *actionRow = new QHBoxLayout();
        actionRow->setSpacing(10);
        actionRow->addWidget(m_connectButton);
        actionRow->addWidget(m_disconnectButton);
        controlLayout->addLayout(pathRow);
        controlLayout->addLayout(actionRow);
        controlLayout->addWidget(m_stateLabel);

        topRow->addWidget(m_configsBox, 2);
        topRow->addWidget(m_controlBox, 3);

        m_logBox = new QGroupBox(root);
        m_logBox->setObjectName("logBox");
        auto *logLayout = new QVBoxLayout(m_logBox);
        auto *logHeader = new QHBoxLayout();
        logHeader->setContentsMargins(0, 0, 0, 0);
        logHeader->addStretch();
        m_hideLogsBtn = new QToolButton(m_logBox);
        m_hideLogsBtn->setObjectName("hideLogsButton");
        m_hideLogsBtn->setText("×");
        m_hideLogsBtn->setToolTip(tr("Hide logs"));
        m_hideLogsBtn->setAutoRaise(true);
        logHeader->addWidget(m_hideLogsBtn);
        logLayout->addLayout(logHeader);
        m_logView = new QTextEdit(m_logBox);
        m_logView->setReadOnly(true);
        logLayout->addWidget(m_logView);

        layout->addLayout(topRow);
        layout->addWidget(m_logBox, 1);

        setCentralWidget(root);
        setStyleSheet(
                "QMainWindow{background:#f4f6fb;}"
                "QGroupBox{background:#ffffff;border:1px solid #dde3ee;border-radius:12px;margin-top:12px;"
                "font-weight:600;color:#1f2a44;}"
                "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;}"
                "QListWidget,QTextEdit,QLineEdit{background:#ffffff;border:1px solid #d7deea;border-radius:10px;padding:8px;}"
                "QPushButton{background:#edf2ff;border:1px solid #d0daf0;border-radius:10px;padding:8px 12px;color:#1e2a42;}"
                "QPushButton:hover{background:#e5ecff;}"
                "QPushButton#connectButton{background:#1c78ff;color:#ffffff;border:1px solid #1c78ff;font-weight:700;}"
                "QPushButton#connectButton:hover{background:#1465df;}"
                "QPushButton#disconnectButton{background:#ffffff;color:#2c3a55;border:1px solid #c9d4ea;font-weight:600;}"
                "QLabel#stateLabel{background:#ecf8f1;color:#176a3a;border:1px solid #c7ead5;border-radius:10px;padding:8px;font-weight:700;}"
                "QToolButton{border:none;padding:4px;border-radius:6px;}"
                "QToolButton:hover{background:#e8edf7;}"
                "QMenuBar{background:#f4f6fb;}"
                "QStatusBar{background:#f4f6fb;color:#415170;}"
        );

        statusBar()->showMessage("Ready");

        m_vpnClient = new QtTrustTunnelClient(this);
        m_vpnClient->setLogLevel(m_appSettings.log_level);

        // Forward core logs to UI with current log level threshold
        const ag::LogLevel uiLogLevel = parseLogLevel(m_appSettings.log_level);
        ag::Logger::set_callback([this, uiLogLevel](ag::LogLevel level, std::string_view msg) {
            if (level > uiLogLevel) {
                return;
            }
            const QString line = QString::fromUtf8(msg.data(), static_cast<int>(msg.size()));
            QMetaObject::invokeMethod(this, [this, line]() {
                log(QStringLiteral("[core] ") + line);
            }, Qt::QueuedConnection);
        });

        m_addConfigButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
        m_removeConfigButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
        m_pingConfigButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
        m_browseButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
        m_viewConfigButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
        m_connectButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_disconnectButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            auto *menu = new QMenu(this);
            auto *openAction = menu->addAction("Open");
            auto *exitAction = menu->addAction("Exit");

            m_tray = new QSystemTrayIcon(this);
            m_tray->setIcon(makeAppIcon());
            m_tray->setContextMenu(menu);
            m_tray->show();

            connect(openAction, &QAction::triggered, this, [this]() {
                showNormal();
                raise();
                activateWindow();
            });
            connect(exitAction, &QAction::triggered, this, [this]() {
                m_forceExit = true;
                if (m_vpnClient) {
                    m_vpnClient->disconnectVpn();
                }
                qApp->quit();
            });
            connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    raise();
                    activateWindow();
                }
            });

            qApp->setQuitOnLastWindowClosed(false);
        }
    }

    void setupLogic() {
        auto syncLogsVisibility = [this](bool on) {
            m_logBox->setVisible(on);
            m_appSettings.show_logs_panel = on;
            saveAppSettings(m_appSettings);
            applyLanguage(m_currentLang);
        };

        connect(m_toggleLogsAction, &QAction::toggled, this, syncLogsVisibility);

        if (m_hideLogsBtn) {
            connect(m_hideLogsBtn, &QToolButton::clicked, this, [this, syncLogsVisibility]() {
                if (m_toggleLogsAction) {
                    m_toggleLogsAction->setChecked(false);
                }
                syncLogsVisibility(false);
            });
        }

        connect(m_langEnAction, &QAction::triggered, this, [this]() {
            applyLanguage("en");
            m_appSettings.language = "en";
            saveAppSettings(m_appSettings);
        });
        connect(m_langRuAction, &QAction::triggered, this, [this]() {
            applyLanguage("ru");
            m_appSettings.language = "ru";
            saveAppSettings(m_appSettings);
        });
        connect(m_settingsAction, &QAction::triggered, this, [this]() {
            SettingsDialog dlg(m_currentLang, m_appSettings, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_appSettings.save_logs = dlg.saveLogs();
                m_appSettings.log_level = dlg.logLevel();
                const QString newPath = dlg.logPath();
                if (!newPath.isEmpty()) {
                    m_appSettings.log_path = newPath;
                }
                m_appSettings.theme_mode = dlg.themeMode();
                m_appSettings.auto_connect_on_start = dlg.autoConnectOnStart();
                m_appSettings.notify_on_state = dlg.notifyOnState();
                m_appSettings.notify_only_errors = dlg.notifyOnlyErrors();
                m_appSettings.killswitch_enabled = dlg.killswitchEnabled();
                m_appSettings.strict_certificate_check = dlg.strictCertificateCheck();
                m_appSettings.show_logs_panel = dlg.showLogsPanel();
                m_appSettings.show_traffic_in_status = dlg.showTrafficInStatus();
                m_appSettings.routing_enabled = dlg.routingEnabled();
                m_appSettings.routing_mode = dlg.routingMode();
                if (!dlg.routingSourceUrl().isEmpty()) m_appSettings.routing_source_url = dlg.routingSourceUrl();
                if (!dlg.routingCachePath().isEmpty()) m_appSettings.routing_cache_path = dlg.routingCachePath();
                m_vpnClient->setLogLevel(m_appSettings.log_level);
                saveAppSettings(m_appSettings);
                applyTheme();
                if (m_toggleLogsAction) m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
                if (!m_appSettings.show_traffic_in_status) statusBar()->clearMessage();
                statusBar()->showMessage(tr("Settings saved"), 2000);
            }
        });
        connect(m_settingsMenuAction, &QAction::triggered, this, [this]() {
            SettingsDialog dlg(m_currentLang, m_appSettings, this);
            if (dlg.exec() == QDialog::Accepted) {
                m_appSettings.save_logs = dlg.saveLogs();
                m_appSettings.log_level = dlg.logLevel();
                const QString newPath = dlg.logPath();
                if (!newPath.isEmpty()) {
                    m_appSettings.log_path = newPath;
                }
                m_appSettings.theme_mode = dlg.themeMode();
                m_appSettings.auto_connect_on_start = dlg.autoConnectOnStart();
                m_appSettings.notify_on_state = dlg.notifyOnState();
                m_appSettings.notify_only_errors = dlg.notifyOnlyErrors();
                m_appSettings.killswitch_enabled = dlg.killswitchEnabled();
                m_appSettings.strict_certificate_check = dlg.strictCertificateCheck();
                m_appSettings.show_logs_panel = dlg.showLogsPanel();
                m_appSettings.show_traffic_in_status = dlg.showTrafficInStatus();
                m_appSettings.routing_enabled = dlg.routingEnabled();
                m_appSettings.routing_mode = dlg.routingMode();
                if (!dlg.routingSourceUrl().isEmpty()) m_appSettings.routing_source_url = dlg.routingSourceUrl();
                if (!dlg.routingCachePath().isEmpty()) m_appSettings.routing_cache_path = dlg.routingCachePath();
                m_vpnClient->setLogLevel(m_appSettings.log_level);
                saveAppSettings(m_appSettings);
                applyTheme();
                if (m_toggleLogsAction) m_toggleLogsAction->setChecked(m_appSettings.show_logs_panel);
                if (!m_appSettings.show_traffic_in_status) statusBar()->clearMessage();
                statusBar()->showMessage(tr("Settings saved"), 2000);
            }
        });
        connect(m_quitAction, &QAction::triggered, this, [this]() {
            m_forceExit = true;
            close();
        });
        connect(m_importDeeplinkAction, &QAction::triggered, this, [this]() {
            bool ok = false;
            const QString link = QInputDialog::getText(
                    this, tr("Import Deeplink"), tr("Paste deeplink URL:"), QLineEdit::Normal, QString(), &ok);
            if (ok && !link.trimmed().isEmpty()) {
                importConfigFromDeeplink(link);
            }
        });
        connect(m_createConfigAction, &QAction::triggered, this, [this]() {
            createConfigFromTemplate();
        });

        connect(m_addConfigButton, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                    this, tr("Select TrustTunnel config"), QString(), tr("TOML files (*.toml);;All files (*)"));
            if (path.isEmpty()) {
                return;
            }
            const QString normalized = QFileInfo(path).absoluteFilePath();
            m_configPath->setText(normalized);
            addCurrentToStorage();
            refreshStoredList();
        });

        connect(m_removeConfigButton, &QPushButton::clicked, this, [this]() {
            QListWidgetItem *item = m_configsList->currentItem();
            if (!item) {
                return;
            }
            const QString selected = item->text();
            QStringList configs = loadStoredConfigs();
            configs.removeAll(selected);
            saveStoredConfigs(configs);
            if (m_configPath->text() == selected) {
                m_configPath->clear();
            }
            refreshStoredList();
        });

        connect(m_pingConfigButton, &QPushButton::clicked, this, [this]() {
            QString path = m_configPath->text();
            if (path.isEmpty() && m_configsList->currentItem()) {
                path = m_configsList->currentItem()->text();
            }
            if (path.isEmpty()) {
                log(tr("Ping: choose config first"));
                return;
            }
            log(tr("Ping %1 ...").arg(path));
            log(tr("Ping result: %1").arg(pingConfigFile(path)));
        });

        connect(m_configsList, &QListWidget::itemSelectionChanged, this, [this]() {
            QListWidgetItem *item = m_configsList->currentItem();
            if (item) {
                m_configPath->setText(item->text());
            }
        });

        connect(m_browseButton, &QPushButton::clicked, this, [this]() {
            const QString path = QFileDialog::getOpenFileName(
                    this, tr("Select TrustTunnel config"), QString(), tr("TOML files (*.toml);;All files (*)"));
            if (!path.isEmpty()) {
                const QString normalized = QFileInfo(path).absoluteFilePath();
                m_configPath->setText(normalized);
                addCurrentToStorage();
                refreshStoredList();
            }
        });

        connect(m_viewConfigButton, &QPushButton::clicked, this, [this]() {
            const QString path = m_configPath->text().trimmed();
            if (path.isEmpty()) {
                QMessageBox::information(this, tr("Config"), tr("Select config file first."));
                return;
            }
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                QMessageBox::warning(this, tr("Config"), tr("Failed to open config file."));
                return;
            }
            auto *dlg = new QDialog(this);
            dlg->setWindowTitle(tr("Config Preview"));
            dlg->resize(860, 560);
            auto *v = new QVBoxLayout(dlg);
            auto *tabs = new QTabWidget(dlg);
            auto *summary = new QTextBrowser(dlg);
            summary->setHtml(buildConfigSummaryHtml(path));
            auto *validation = new QTextBrowser(dlg);
            validation->setHtml(buildConfigValidationHtml(path));
            auto *raw = new QTextEdit(dlg);
            raw->setReadOnly(true);
            raw->setPlainText(QString::fromUtf8(f.readAll()));
            tabs->addTab(summary, tr("Overview"));
            tabs->addTab(validation, tr("Validation"));
            tabs->addTab(raw, tr("Raw"));
            v->addWidget(tabs);
            dlg->exec();
            dlg->deleteLater();
        });

        connect(m_connectButton, &QPushButton::clicked, this, [this]() {
            if (m_configPath->text().isEmpty()) {
                QMessageBox::warning(this, tr("Config Required"), tr("Select config file first."));
                return;
            }
            // reset counters on a fresh session
            m_bytesRx = 0;
            m_bytesTx = 0;
            statusBar()->showMessage(tr("Preparing routing rules..."), 1500);
#ifndef _WIN32
            if (!m_isRoot) {
                log(tr("Restarting app with sudo for VPN privileges"));
                relaunchElevated();
                return;
            }
#endif
            std::vector<std::string> includeRoutes;
            std::vector<std::string> excludeRoutes;
            if (!prepareRoutingRules(includeRoutes, excludeRoutes)) {
                statusBar()->showMessage(tr("Routing update failed"), 3000);
                return;
            }
            statusBar()->showMessage(tr("Routing rules ready"), 1500);
            if (!m_vpnClient->loadConfigFromFile(m_configPath->text())) {
                statusBar()->showMessage(tr("Config load failed"), 3000);
                return;
            }
            m_vpnClient->setRoutingRules(includeRoutes, excludeRoutes);
            log(tr("Connecting VPN..."));
            statusBar()->showMessage(tr("Connecting..."), 1500);
            m_vpnClient->connectVpn();
            addCurrentToStorage();
        });

        connect(m_disconnectButton, &QPushButton::clicked, this, [this]() {
            m_vpnClient->disconnectVpn();
            log(tr("Disconnect requested"));
        });

        auto updateStateUi = [this](QtTrustTunnelClient::State s) {
            switch (s) {
            case QtTrustTunnelClient::State::Connecting:
                m_stateLabel->setText(tr("VPN: Connecting"));
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                break;
            case QtTrustTunnelClient::State::Connected:
                m_stateLabel->setText(tr("VPN: Connected"));
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                statusBar()->showMessage(tr("VPN connected"), 2000);
                break;
            case QtTrustTunnelClient::State::Reconnecting:
                m_stateLabel->setText(tr("VPN: Reconnecting"));
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(true);
                m_statsTimer.start(1500);
                break;
            case QtTrustTunnelClient::State::Disconnecting:
                m_stateLabel->setText(tr("VPN: Disconnecting"));
                m_connectButton->setEnabled(false);
                m_disconnectButton->setEnabled(false);
                m_statsTimer.stop();
                break;
            case QtTrustTunnelClient::State::Error:
                m_stateLabel->setText(tr("VPN: Error"));
                m_connectButton->setEnabled(true);
                m_disconnectButton->setEnabled(false);
                m_statsTimer.stop();
                break;
            case QtTrustTunnelClient::State::Disconnected:
            default:
                m_stateLabel->setText(tr("VPN: Disconnected"));
                m_connectButton->setEnabled(true);
                m_disconnectButton->setEnabled(false);
                m_statsTimer.stop();
                break;
            }
        };

        connect(m_vpnClient, &QtTrustTunnelClient::stateChanged, this, updateStateUi);
        // Sync UI with the actual current state at startup
        updateStateUi(m_vpnClient->state());

        connect(m_vpnClient, &QtTrustTunnelClient::vpnConnected, this, [this]() {
            log(tr("VPN connected"));
            if (m_appSettings.notify_on_state && !m_appSettings.notify_only_errors && m_tray) {
                m_tray->showMessage(windowTitle(), tr("VPN connected"), QSystemTrayIcon::Information, 2000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::vpnDisconnected, this, [this]() {
            log(tr("VPN disconnected"));
            if (m_appSettings.notify_on_state && !m_appSettings.notify_only_errors && m_tray) {
                m_tray->showMessage(windowTitle(), tr("VPN disconnected"), QSystemTrayIcon::Information, 2000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::vpnError, this, [this](const QString &msg) {
            log(tr("VPN error: %1").arg(msg));
            statusBar()->showMessage(msg, 4000);
            if (m_appSettings.notify_on_state && m_tray) {
                m_tray->showMessage(windowTitle(), msg, QSystemTrayIcon::Critical, 4000);
            }
        });
        connect(m_vpnClient, &QtTrustTunnelClient::connectionInfo, this, [this](const QString &msg) {
            log(tr("Connection: %1").arg(msg));
        });
        connect(m_vpnClient, &QtTrustTunnelClient::clientOutput, this, [this](const QString &bytes) {
            bool ok = false;
            const quint64 b = bytes.toULongLong(&ok);
            if (ok) {
                m_bytesRx += b;
            }
            log(tr("Output chunk: %1 bytes").arg(bytes));
        });

        m_statsTimer.setSingleShot(false);
        m_statsTimer.setInterval(1500);
        connect(&m_statsTimer, &QTimer::timeout, this, [this]() {
            if (!m_appSettings.show_traffic_in_status) return;
            statusBar()->showMessage(tr("Traffic Rx: %1 KB  Tx: %2 KB")
                                             .arg(QString::number(m_bytesRx / 1024))
                                             .arg(QString::number(m_bytesTx / 1024)),
                    1400);
        });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (m_appSettings.theme_mode == "system") {
                applyTheme();
            }
        });
#else
        qApp->installEventFilter(this);
#endif
    }

    void applyLanguage(const QString &lang) {
        m_currentLang = lang;
        const bool ru = (lang == "ru");
        if (m_langEnAction) m_langEnAction->setChecked(!ru);
        if (m_langRuAction) m_langRuAction->setChecked(ru);
        if (m_appMenu) m_appMenu->setTitle(ru ? "Приложение" : "App");
        if (m_settingsMenu) m_settingsMenu->setTitle(ru ? "Настройки" : "Settings");
        if (m_viewMenu) m_viewMenu->setTitle(ru ? "Вид" : "View");
        if (m_languageMenu) m_languageMenu->setTitle(ru ? "Язык" : "Language");
        m_configsBox->setTitle(ru ? "Сохраненные конфиги" : "Saved Configs");
        m_controlBox->setTitle(ru ? "Подключение" : "Connection");
        m_logBox->setTitle(ru ? "Логи" : "Logs");

        m_addConfigButton->setText(ru ? "Добавить" : "Add");
        m_removeConfigButton->setText(ru ? "Удалить" : "Remove");
        m_pingConfigButton->setText(ru ? "Пинг" : "Ping");
        m_browseButton->setText(ru ? "Обзор..." : "Browse...");
        m_viewConfigButton->setText(ru ? "Просмотр" : "View Config");
        m_connectButton->setText(ru ? "Старт VPN" : "Start VPN");
        m_disconnectButton->setText(ru ? "Стоп VPN" : "Stop VPN");
        if (m_settingsAction) m_settingsAction->setText(ru ? "Настройки" : "Settings");
        if (m_importDeeplinkAction) m_importDeeplinkAction->setText(ru ? "Импорт Deeplink" : "Import Deeplink");
        if (m_createConfigAction) m_createConfigAction->setText(ru ? "Создать конфиг" : "Create Config");
        if (m_settingsMenuAction) m_settingsMenuAction->setText(ru ? "Открыть настройки" : "Open Settings");
        if (m_quitAction) m_quitAction->setText(ru ? "Выход" : "Quit");
        if (m_toggleLogsAction) {
            m_toggleLogsAction->setText(m_toggleLogsAction->isChecked() ? (ru ? "Скрыть логи" : "Hide Logs")
                                                                         : (ru ? "Показать логи" : "Show Logs"));
        }

        const QString text = m_stateLabel->text();
        if (text.contains("Connecting", Qt::CaseInsensitive)
                || text.contains("соедин", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: подключение" : "VPN: Connecting");
        } else if (text.contains("Connected", Qt::CaseInsensitive)
                || text.contains("подключен", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: подключено" : "VPN: Connected");
        } else if (text.contains("Reconnecting", Qt::CaseInsensitive)
                || text.contains("переподк", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: переподключение" : "VPN: Reconnecting");
        } else if (text.contains("Error", Qt::CaseInsensitive)
                || text.contains("ошиб", Qt::CaseInsensitive)) {
            m_stateLabel->setText(ru ? "VPN: ошибка" : "VPN: Error");
        } else {
            m_stateLabel->setText(ru ? "VPN: отключено" : "VPN: Disconnected");
        }

        const QString baseTitle = ru ? "FireTunnel" : "FireTunnel";
        const QString full = ru ? " (Полный режим)" : " (Full mode)";
        const bool elevated = m_isRoot
#ifdef _WIN32
                || isProcessElevated()
#endif
                ;
        setWindowTitle(elevated ? baseTitle + full : baseTitle);
    }

    void enforceFirstRunElevation() {
#ifdef _WIN32
        if (m_appSettings.first_run_checked) {
            return;
        }
        if (!isProcessElevated()) {
            const auto res = QMessageBox::warning(this,
                    tr("Administrator privileges required"),
                    tr("For proper VPN setup we need to run as Administrator on first launch. Restart now as admin?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (res == QMessageBox::Yes) {
                m_appSettings.first_run_checked = true;
                saveAppSettings(m_appSettings);
                relaunchElevated();
            }
        } else {
            m_appSettings.first_run_checked = true;
            saveAppSettings(m_appSettings);
        }
#endif
    }

    void appendLogChunk(const QByteArray &chunk) {
        if (chunk.isEmpty()) {
            return;
        }
        if (m_appSettings.save_logs && !m_appSettings.log_path.isEmpty()) {
            QFile f(m_appSettings.log_path);
            QFileInfo info(f);
            QDir().mkpath(info.absolutePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
                f.write(chunk);
            }
        }
        m_logView->moveCursor(QTextCursor::End);
        m_logView->insertPlainText(QString::fromLocal8Bit(chunk));
        m_logView->ensureCursorVisible();
    }

    void log(const QString &line) {
        appendLogChunk(line.toUtf8() + '\n');
    }

    void applyTheme() {
        auto makeLightPalette = []() {
            QPalette p;
            p.setColor(QPalette::Window, QColor(244, 246, 251));
            p.setColor(QPalette::WindowText, QColor(31, 42, 68));
            p.setColor(QPalette::Base, QColor(255, 255, 255));
            p.setColor(QPalette::AlternateBase, QColor(248, 250, 255));
            p.setColor(QPalette::Text, QColor(31, 42, 68));
            p.setColor(QPalette::Button, QColor(237, 242, 255));
            p.setColor(QPalette::ButtonText, QColor(31, 42, 68));
            p.setColor(QPalette::Highlight, QColor(28, 120, 255));
            p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
            return p;
        };
        auto makeDarkPalette = []() {
            QPalette p;
            p.setColor(QPalette::Window, QColor(24, 27, 34));
            p.setColor(QPalette::WindowText, QColor(233, 238, 248));
            p.setColor(QPalette::Base, QColor(32, 36, 45));
            p.setColor(QPalette::AlternateBase, QColor(44, 49, 61));
            p.setColor(QPalette::Text, QColor(233, 238, 248));
            p.setColor(QPalette::Button, QColor(41, 48, 63));
            p.setColor(QPalette::ButtonText, QColor(233, 238, 248));
            p.setColor(QPalette::Highlight, QColor(74, 154, 255));
            p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
            return p;
        };
        const QString lightQss =
                "QMainWindow{background:#f4f6fb;}"
                "QGroupBox{background:#ffffff;border:1px solid #dde3ee;border-radius:12px;margin-top:12px;font-weight:600;color:#1f2a44;}"
                "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;}"
                "QListWidget,QTextEdit,QLineEdit{background:#ffffff;border:1px solid #d7deea;border-radius:10px;padding:8px;}"
                "QPushButton{background:#edf2ff;border:1px solid #d0daf0;border-radius:10px;padding:8px 12px;color:#1e2a42;}"
                "QPushButton:hover{background:#e5ecff;}"
                "QPushButton#connectButton{background:#1c78ff;color:#ffffff;border:1px solid #1c78ff;font-weight:700;}"
                "QPushButton#connectButton:hover{background:#1465df;}"
                "QPushButton#disconnectButton{background:#ffffff;color:#2c3a55;border:1px solid #c9d4ea;font-weight:600;}"
                "QLabel#stateLabel{background:#ecf8f1;color:#176a3a;border:1px solid #c7ead5;border-radius:10px;padding:8px;font-weight:700;}"
                "QMenuBar{background:#f4f6fb;}"
                "QStatusBar{background:#f4f6fb;color:#415170;}";
        const QString darkQss =
                "QMainWindow{background:#181b22;}"
                "QGroupBox{background:#222732;border:1px solid #333b4d;border-radius:12px;margin-top:12px;font-weight:600;color:#e9eef8;}"
                "QGroupBox::title{subcontrol-origin:margin;left:12px;padding:0 4px;}"
                "QListWidget,QTextEdit,QLineEdit{background:#20242d;border:1px solid #394257;border-radius:10px;padding:8px;color:#e9eef8;}"
                "QPushButton{background:#2b3342;border:1px solid #42506a;border-radius:10px;padding:8px 12px;color:#e9eef8;}"
                "QPushButton:hover{background:#354057;}"
                "QPushButton#connectButton{background:#2b7cff;color:#ffffff;border:1px solid #2b7cff;font-weight:700;}"
                "QPushButton#connectButton:hover{background:#2167da;}"
                "QPushButton#disconnectButton{background:#2a303d;color:#d7e0f2;border:1px solid #44506a;font-weight:600;}"
                "QLabel#stateLabel{background:#1f3a2b;color:#8ee0b1;border:1px solid #2f5f46;border-radius:10px;padding:8px;font-weight:700;}"
                "QMenuBar{background:#181b22;color:#e9eef8;}"
                "QStatusBar{background:#181b22;color:#9fb0d0;}";

        bool dark = false;
        if (m_appSettings.theme_mode == "dark") {
            dark = true;
        } else if (m_appSettings.theme_mode == "light") {
            dark = false;
        } else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            dark = (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
#else
            const QColor base = qApp->palette().color(QPalette::Window);
            dark = (base.lightness() < 128);
#endif
        }

        qApp->setStyle("Fusion");
        qApp->setPalette(dark ? makeDarkPalette() : makeLightPalette());
        setStyleSheet(dark ? darkQss : lightQss);
    }

    void refreshStoredList() {
        const QString current = m_configPath->text();
        m_configsList->clear();
        for (const QString &p : loadStoredConfigs()) {
            m_configsList->addItem(p);
        }
        for (int i = 0; i < m_configsList->count(); ++i) {
            if (m_configsList->item(i)->text() == current) {
                m_configsList->setCurrentRow(i);
                break;
            }
        }
    }

    void addCurrentToStorage() {
        if (m_configPath->text().isEmpty()) {
            return;
        }
        QStringList configs = loadStoredConfigs();
        if (!configs.contains(m_configPath->text())) {
            configs.append(m_configPath->text());
            saveStoredConfigs(configs);
            refreshStoredList();
        }
    }

private:
    bool m_forceExit = false;

    QGroupBox *m_configsBox = nullptr;
    QGroupBox *m_controlBox = nullptr;
    QGroupBox *m_logBox = nullptr;

    QListWidget *m_configsList = nullptr;
    QLineEdit *m_configPath = nullptr;
    QPushButton *m_addConfigButton = nullptr;
    QPushButton *m_removeConfigButton = nullptr;
    QPushButton *m_pingConfigButton = nullptr;
    QPushButton *m_browseButton = nullptr;
    QPushButton *m_viewConfigButton = nullptr;
    QPushButton *m_connectButton = nullptr;
    QPushButton *m_disconnectButton = nullptr;
    QLabel *m_stateLabel = nullptr;
    QTextEdit *m_logView = nullptr;

    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_appMenu = nullptr;
    QMenu *m_settingsMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_importDeeplinkAction = nullptr;
    QAction *m_createConfigAction = nullptr;
    QAction *m_settingsMenuAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_toggleLogsAction = nullptr;
    QAction *m_langEnAction = nullptr;
    QAction *m_langRuAction = nullptr;
    QString m_currentLang = "en";
    QToolButton *m_hideLogsBtn = nullptr;

    bool m_isRoot = false;
    quint64 m_bytesRx = 0;
    quint64 m_bytesTx = 0;
    QTimer m_statsTimer;
    QtTrustTunnelClient *m_vpnClient = nullptr;
    AppSettings m_appSettings;
};

QMainWindow *createMainWindow() {
    auto *w = new MainWindow();
    w->setWindowIcon(makeAppIcon());
    return w;
}

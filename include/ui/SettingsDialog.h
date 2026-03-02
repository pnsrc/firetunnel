#pragma once

#include <QDialog>
#include <QWidget>
#include <QRadioButton>

#include "AppSettings.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QListWidgetItem;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(const QString &lang, const AppSettings &settings, QWidget *parent = nullptr);

    bool saveLogs() const;
    QString logLevel() const;
    QString logPath() const;
    QString themeMode() const;
    bool autoConnectOnStart() const;
    bool showLogsPanel() const;
    bool showTrafficInStatus() const;
    bool notifyOnState() const;
    bool notifyOnlyErrors() const;
    bool killswitchEnabled() const;
    bool strictCertificateCheck() const;
    bool routingEnabled() const;
    QString routingMode() const;
    QString routingSourceUrl() const;
    QString routingCachePath() const;

    /// Returns true if user requested a tunnel adapter reinstall.
    bool reinstallTunnelsRequested() const;
    /// Returns true if user requested a DNS flush.
    bool flushDnsRequested() const;
    /// Returns true if user requested SSL session cache clear.
    bool clearSslCacheRequested() const;
    /// Returns true if user requested a full settings reset.
    bool resetSettingsRequested() const;

signals:
    void advancedAction(const QString &action);

private:
    QCheckBox *m_saveLogsCheck = nullptr;
    QComboBox *m_logLevelCombo = nullptr;
    QCheckBox *m_showLogsPanelCheck = nullptr;
    QCheckBox *m_showTrafficCheck = nullptr;
    QCheckBox *m_notifyOnStateCheck = nullptr;
    QCheckBox *m_notifyErrorsOnlyCheck = nullptr;
    QCheckBox *m_killswitchCheck = nullptr;
    QCheckBox *m_strictCertCheck = nullptr;
    QComboBox *m_themeModeCombo = nullptr;
    QLineEdit *m_logPathEdit = nullptr;
    QCheckBox *m_autoConnectCheck = nullptr;
    QCheckBox *m_routingEnableCheck = nullptr;
    QRadioButton *m_routingTunnelRadio = nullptr;
    QRadioButton *m_routingBypassRadio = nullptr;
    QLineEdit *m_routingUrlEdit = nullptr;
    QLineEdit *m_routingCacheEdit = nullptr;

    bool m_reinstallTunnels = false;
    bool m_flushDns = false;
    bool m_clearSslCache = false;
    bool m_resetSettings = false;

    QListWidget *m_navList = nullptr;
    QStackedWidget *m_stack = nullptr;
};
